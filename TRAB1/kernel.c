// kernel.c - processo kernel que gerencia escalonamento RR + I/O com SHM e FIFO

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define MAXN  6
#define MINN  3
#define FIFO_PATH "/tmp/so_trab1_iofifo"

enum { ST_NEW=0, ST_READY, ST_RUNNING, ST_WAITING, ST_DONE };

struct shm_data {
  int  nprocs;
  pid_t app_pid[MAXN];
  int  pc[MAXN];
  int  want_io[MAXN];
  int  io_type[MAXN];     // 0=READ, 1=WRITE (parâmetro da syscall)
  int  done;

  // Estado do dispositivo (controlado pelo InterController)
  int  d1_busy;
  pid_t io_inflight_pid;  // pid atualmente em atendimento

  // Resultado do último atendimento concluído (escrito pelo InterController)
  pid_t io_done_pid;
  int   io_done_type;     // 0=READ, 1=WRITE
};

// ===== util de tempo (ms relativos) =====
static long t0_ms = -1;
static long now_ms(void){
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec*1000L + ts.tv_nsec/1000000L;
}
static void init_t0(void){ if (t0_ms < 0) t0_ms = now_ms(); }
static long rel_ms(void){ return now_ms() - t0_ms; }
// =======================================

static int shm_id = -1;
static struct shm_data *shm = NULL;

static int num_procs = 3;
static pid_t proc_pids[MAXN];
static int proc_state[MAXN];

static int current_idx = -1;
static int time_slice_seconds = 1;
static int time_slice_left = 0;
static int run_duration_seconds = 15;

static pid_t inter_controller_pid = -1;
static int fifo_fd = -1;
static pid_t self_pid;

static volatile sig_atomic_t got_irq0 = 0;
static volatile sig_atomic_t got_irq1 = 0;
static volatile sig_atomic_t stop_flag = 0; // para SIGINT/SIGTERM

static void set_state(int i, int st) { proc_state[i] = st; }

static int pick_next_ready(void) {
  if (num_procs <= 0) return -1;
  int start = (current_idx < 0 ? 0 : (current_idx + 1) % num_procs);
  int i = start;
  for (;;) {
    if (proc_state[i] == ST_READY) return i;
    i = (i + 1) % num_procs;
    if (i == start) break;
  }
  return -1;
}

static void dispatch_index(int idx) {
  if (idx < 0) return;
  current_idx = idx;
  set_state(idx, ST_RUNNING);
  printf("[KRL %ldms] DESPACHE -> idx=%d pid=%d\n", rel_ms(), idx, (int)proc_pids[idx]);
  fflush(stdout);
  kill(proc_pids[idx], SIGCONT);
}

static void preempt_running_to_ready(void) {
  if (current_idx < 0) return;
  int i = current_idx;
  printf("[KRL %ldms] PREEMPÇÃO -> idx=%d pid=%d (sai da CPU)\n", rel_ms(), i, (int)proc_pids[i]);
  fflush(stdout);
  kill(proc_pids[i], SIGSTOP);
  set_state(i, ST_READY);
  current_idx = -1;
}

static void block_running_for_io(int io_type) {
  if (current_idx < 0) return;
  int i = current_idx;
  printf("[KRL %ldms] BLOQUEIO (I/O %s) -> idx=%d pid=%d | ENFILEIRA\n",
         rel_ms(), io_type==0?"READ":"WRITE", i, (int)proc_pids[i]);
  fflush(stdout);

  kill(proc_pids[i], SIGSTOP);
  set_state(i, ST_WAITING);
  current_idx = -1;

  if (fifo_fd >= 0) {
    dprintf(fifo_fd, "%d %d\n", (int)proc_pids[i], io_type); // "PID TIPO\n"
  }
  // NÃO setar d1_busy / io_inflight_pid aqui — isso é do InterController.
}

static void on_irq0(int sig) { (void)sig; got_irq0 = 1; }
static void on_irq1(int sig) { (void)sig; got_irq1 = 1; }
static void on_stop(int sig) { (void)sig; stop_flag = 1; }

static void install_handlers(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_irq0;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, NULL);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_irq1;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR2, &sa, NULL);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_stop;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

static void shared_memory_init(int n) {
  shm_id = shmget(IPC_PRIVATE, sizeof(struct shm_data), IPC_CREAT | IPC_EXCL | 0600);
  if (shm_id == -1) { perror("shmget"); exit(1); }
  shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1) { perror("shmat"); exit(1); }
  memset(shm, 0, sizeof(*shm));
  shm->nprocs = n;
}

static void fifo_make_only(void) {
  unlink(FIFO_PATH);
  if (mkfifo(FIFO_PATH, 0600) == -1) {
    perror("mkfifo"); exit(1);
  }
}

static void fifo_open_writer_blocking(void) {
  fifo_fd = open(FIFO_PATH, O_WRONLY); // bloqueia até o leitor existir
  if (fifo_fd < 0) { perror("open fifo wr"); exit(1); }
}

static void spawn_inter_controller(void) {
  pid_t pid = fork();
  if (pid < 0) { perror("fork IC"); exit(1); }
  if (pid == 0) {
    char shmid_s[32], kpid_s[32];
    snprintf(shmid_s, sizeof(shmid_s), "%d", shm_id);
    snprintf(kpid_s,  sizeof(kpid_s), "%d", (int)self_pid);
    execlp("./inter_controller", "./inter_controller", shmid_s, kpid_s, (char*)NULL);
    _exit(127);
  }
  inter_controller_pid = pid;
}

static void spawn_apps(void) {
  for (int i = 0; i < num_procs; i++) {
    pid_t p = fork();
    if (p < 0) { perror("fork app"); exit(1); }
    if (p == 0) {
      char shmid_s[32];
      snprintf(shmid_s, sizeof(shmid_s), "%d", shm_id);
      execlp("./app", "./app", shmid_s, (char*)NULL);
      _exit(127);
    }
    proc_pids[i] = p;
    shm->app_pid[i] = p;
    set_state(i, ST_READY);
    kill(p, SIGSTOP);
  }
}

static int idx_of_pid(pid_t p) {
  for (int i = 0; i < num_procs; i++) if (proc_pids[i] == p) return i;
  return -1;
}

static int count_app_blocks(int argc, char **argv) {
  int count = 0;
  for (int i = 3; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      if (i + 1 < argc) { count++; i++; }
    }
  }
  return count;
}

int main(int argc, char **argv) {
  self_pid = getpid();
  init_t0();

  // Uso: ./kernel <quantum_s> <duracao_s> -- ./app [-- ./app]...
  time_slice_seconds   = (argc > 1) ? atoi(argv[1]) : 1;
  run_duration_seconds = (argc > 2) ? atoi(argv[2]) : 15;

  int blocks = count_app_blocks(argc, argv);
  if (blocks <= 0) {
    fprintf(stderr, "[KRL] ERRO: uso: ./kernel <q> <dur> -- ./app [-- ./app]...\n");
    return 2;
  }
  if (blocks < MINN || blocks > MAXN) {
    fprintf(stderr, "[KRL] ERRO: número de apps deve ser entre %d e %d (recebido %d)\n",
            MINN, MAXN, blocks);
    return 2;
  }
  num_procs = blocks;

  shared_memory_init(num_procs);
  fifo_make_only();
  install_handlers();

  spawn_inter_controller();
  fifo_open_writer_blocking(); // abre escritor só depois do leitor existir
  spawn_apps();

  printf("[KRL %ldms] INÍCIO | RR+I/O | quantum=%ds | duração=%ds | procs=%d\n",
         rel_ms(), time_slice_seconds, run_duration_seconds, num_procs);
  fflush(stdout);

  int first = pick_next_ready();
  if (first >= 0) { dispatch_index(first); time_slice_left = time_slice_seconds; }

  time_t t0 = time(NULL);

  while (1) {
    if (stop_flag) break;
    if (time(NULL) - t0 >= run_duration_seconds) break;

    pause();

    if (got_irq0) {
      got_irq0 = 0;

      if (current_idx >= 0) {
        int idx = current_idx;
        if (shm->want_io[idx]) {
          int iot = shm->io_type[idx];
          shm->want_io[idx] = 0;
          block_running_for_io(iot);
        }
      }

      if (time_slice_left > 0) time_slice_left--;
      if (time_slice_left == 0) {
        // FIX mínimo: preserve o índice preemptado para o ponto de partida do RR
        int prev = current_idx;
        if (current_idx >= 0) preempt_running_to_ready();
        if (prev >= 0) current_idx = prev; // apenas para pick_next_ready iniciar do próximo
        int nxt = pick_next_ready();
        if (nxt >= 0) {
          dispatch_index(nxt);
          time_slice_left = time_slice_seconds;
        } else {
          current_idx = -1; // ninguém pronto
        }
      }
    }

    if (got_irq1) {
      got_irq1 = 0;
      // Usa SEMPRE io_done_pid/type escritos pelo InterController
      pid_t donep = shm->io_done_pid;
      int   dtype = shm->io_done_type;
      int idx = idx_of_pid(donep);

      if (idx >= 0 && proc_state[idx] == ST_WAITING) {
        printf("[KRL %ldms] DESBLOQUEIO (IRQ1 I/O %s) -> idx=%d pid=%d | PRIORIDADE\n",
               rel_ms(), dtype==0?"READ":"WRITE", idx, (int)donep);
        fflush(stdout);
        if (current_idx >= 0) preempt_running_to_ready();
        set_state(idx, ST_READY);
        dispatch_index(idx);
        time_slice_left = time_slice_seconds;
      }
      // limpa marca de "done" para o próximo
      shm->io_done_pid = 0;
      shm->io_done_type = 0;
    }

    int status; pid_t z;
    while ((z = waitpid(-1, &status, WNOHANG)) > 0) {
      int idx = idx_of_pid(z);
      if (idx >= 0) set_state(idx, ST_DONE);
    }

    int alive = 0;
    for (int i = 0; i < num_procs; i++) if (proc_state[i] != ST_DONE) alive++;
    if (alive == 0) break;
  }

  // encerra
  for (int i = 0; i < num_procs; i++) if (proc_state[i] != ST_DONE) kill(proc_pids[i], SIGKILL);
  for (int i = 0; i < num_procs; i++) waitpid(proc_pids[i], NULL, 0);

  if (inter_controller_pid > 0) kill(inter_controller_pid, SIGTERM);
  if (fifo_fd >= 0) { close(fifo_fd); fifo_fd = -1; unlink(FIFO_PATH); }

  if (shm) {
    shm->done = 1;
    shmdt((void*)shm);
    shmctl(shm_id, IPC_RMID, NULL);
    shm = NULL; shm_id = -1;
  }
  printf("[KRL %ldms] FIM do Kernel\n", rel_ms());
  return 0;
}