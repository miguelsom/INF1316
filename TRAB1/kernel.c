// kernel.c
#define _XOPEN_SOURCE 700
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define MAX_PROCS  32
#define MAX_ARGS   64

#define IRQ0_SIG   SIGUSR1
#define IRQ1_SIG   SIGUSR2

#define ST_READY    1
#define ST_RUNNING  2
#define ST_WAITING  3
#define ST_DONE     4

static const char *FIFO_PATH = "/tmp/inf1316_d1_fifo";
static int fifo_fd = -1;

/* tabelas */
static pid_t proc_pids[MAX_PROCS];
static int   proc_state[MAX_PROCS];
static int   num_procs = 0;

static int time_slice_seconds   = 0;
static int run_duration_seconds = 0;

static int current_index   = -1;
static int time_slice_left = 0;
static void handle_io_requests_from_ready(void);

static int last_irq1_dispatch = -1;  /* idx do processo despachado pelo último IRQ1; -1 = nenhum */


/* SHM */
struct shm_data {
  int    nprocs;
  pid_t  app_pid[MAX_PROCS];
  volatile int pc[MAX_PROCS];
  volatile int want_io[MAX_PROCS];
  volatile int io_type[MAX_PROCS];
  volatile int st[MAX_PROCS];
  int q[MAX_PROCS]; int head, tail;
  int d1_busy;
  pid_t io_inflight_pid;
  time_t t_end;
  int done;
};

static int              shm_id = -1;
static struct shm_data *shm    = NULL;

/* util */
static void int_to_str10(int x, char *buf) {
  char tmp[32]; int i=0, j=0, neg=0;
  if (x==0){ buf[0]='0'; buf[1]=0; return; }
  if (x<0){ neg=1; x=-x; }
  while (x>0 && i<31){ tmp[i++] = (char)('0'+(x%10)); x/=10; }
  if (neg) tmp[i++]='-';
  while (i>0) buf[j++]=tmp[--i];
  buf[j]=0;
}

static void shared_memory_init(int n) {
  shm_id = shmget(IPC_PRIVATE, sizeof(struct shm_data), IPC_CREAT | IPC_EXCL | 0600);
  if (shm_id == -1) { perror("shmget"); exit(1); }
  shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1) { perror("shmat"); shmctl(shm_id, IPC_RMID, NULL); exit(1); }
  memset((void*)shm, 0, sizeof(*shm));
  shm->nprocs = n;
  for (int i = 0; i < n; i++) shm->st[i] = ST_READY;
  shm->head = shm->tail = 0;
}

static void set_state(int idx, int st) {
  proc_state[idx] = st;
  if (shm) shm->st[idx] = st;
}

/* helpers de fila de I/O na SHM */
static int io_queue_size(void){
  int h = shm->head, t = shm->tail;
  int sz = t - h; if (sz < 0) sz += MAX_PROCS;
  return sz;
}
static int io_queue_empty(void) { return shm->head == shm->tail; }
static void io_queue_push(int idx) {
  int before = io_queue_size();
  shm->q[shm->tail % MAX_PROCS] = idx;
  shm->tail = (shm->tail + 1) % MAX_PROCS;
  int after = io_queue_size();
  printf("[KRL] IO-ENQ idx=%d pid=%d queue=%d->%d\n",
         idx, (int)proc_pids[idx], before, after);
  fflush(stdout);
}
static int io_queue_pop(void) {
  if (io_queue_empty()) return -1;
  int before = io_queue_size();
  int idx = shm->q[shm->head % MAX_PROCS];
  shm->head = (shm->head + 1) % MAX_PROCS;
  int after = io_queue_size();
  printf("[KRL] IO-DEQ idx=%d pid=%d queue=%d->%d\n",
         idx, (int)proc_pids[idx], before, after);
  fflush(stdout);
  return idx;
}

/* RR */
static void preempt_current(void) {
  if (current_index >= 0 && proc_state[current_index] == ST_RUNNING) {
    kill(proc_pids[current_index], SIGSTOP);
    set_state(current_index, ST_READY);
  }
}
static void dispatch_index(int idx) {
  current_index = idx;
  set_state(idx, ST_RUNNING);
  kill(proc_pids[idx], SIGCONT);
}
static int pick_next_ready(void) {
  if (num_procs == 0) return -1;
  int start = (current_index + 1 + num_procs) % num_procs;
  for (int step = 0; step < num_procs; step++) {
    int k = (start + step) % num_procs;
    if (proc_state[k] == ST_READY) return k;
  }
  return -1;
}

/* FIFO */
static void ensure_fifo_writable(void){
  if (fifo_fd >= 0) return;
  /* O_RDWR evita depender de leitor do outro lado */
  mkfifo(FIFO_PATH, 0666);
  fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
  printf("[KRL] FIFO open fd=%d (errno=%d)\n", fifo_fd, errno);
  fflush(stdout);
}
static void fifo_send_one_line(void){
  /* caminho 1: tentativa com fd temporário em O_WRONLY|O_NONBLOCK
     (garante borda de “novo writer” para o leitor enxergar dado). */
  int tmp = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
  const char nl = '\n';
  if (tmp >= 0) {
    errno = 0;
    ssize_t w = write(tmp, &nl, 1);
    printf("[KRL] FIFO temp write ret=%ld fd=%d errno=%d\n", (long)w, tmp, errno);
    fflush(stdout);
    close(tmp);
    return;
  }

  /* caminho 2 (fallback): usa o fd persistente em O_RDWR|O_NONBLOCK */
  if (fifo_fd < 0) {
    mkfifo(FIFO_PATH, 0666);
    fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
    printf("[KRL] FIFO open fd=%d (fallback) errno=%d\n", fifo_fd, errno);
    fflush(stdout);
  }
  if (fifo_fd >= 0) {
    errno = 0;
    ssize_t w = write(fifo_fd, &nl, 1);
    printf("[KRL] FIFO write ret=%ld fd=%d errno=%d\n", (long)w, fifo_fd, errno);
    fflush(stdout);
  } else {
    printf("[KRL] FIFO write SKIP (no fd)\n");
    fflush(stdout);
  }
}


/* pedido de I/O do RUNNING atual */
static void handle_io_request_of_current(void) {
  if (current_index < 0 || !shm) return;
  if (shm->want_io[current_index] != 1) return;

  printf("[KRL] IO_REQ current idx=%d pid=%d pc=%d\n",
         current_index, (int)proc_pids[current_index], shm->pc[current_index]);
  fflush(stdout);

  shm->want_io[current_index] = 0;
  set_state(current_index, ST_WAITING);
  io_queue_push(current_index);

  fifo_send_one_line();

  kill(proc_pids[current_index], SIGSTOP);
  current_index = -1;
}

/* Converte pedidos de I/O dos processos que estão READY (parados) */
static void handle_io_requests_from_ready(void) {
  if (!shm) return;

  if (fifo_fd < 0) ensure_fifo_writable();

  for (int i = 0; i < num_procs; i++) {
    if (proc_state[i] == ST_READY && shm->want_io[i] == 1) {
      printf("[KRL] IO_REQ ready  idx=%d pid=%d pc=%d\n",
             i, (int)proc_pids[i], shm->pc[i]);
      fflush(stdout);

      shm->want_io[i] = 0;        /* consome pedido */
      set_state(i, ST_WAITING);   /* bloqueia */
      io_queue_push(i);           /* entra na fila do “dispositivo” */

      fifo_send_one_line();
      /* não precisa SIGSTOP: já está parado */
    }
  }
}

/* IRQ0 */
static void handle_irq0_signal(int sig) {
  (void)sig;
  last_irq1_dispatch = -1;  /* abre janela para próxima priorização por IRQ1 */

  for (int i = 0; i < num_procs; i++) {
    if (proc_state[i] != ST_DONE) {
      int status; pid_t r = waitpid(proc_pids[i], &status, WNOHANG);
      if (r == proc_pids[i]) {
        printf("[KRL] PROC DONE idx=%d pid=%d\n", i, (int)proc_pids[i]); fflush(stdout);
        set_state(i, ST_DONE);
        kill(proc_pids[i], SIGSTOP);
        if (current_index == i) current_index = -1;
      }
    }
  }

  handle_io_request_of_current();    /* corrente (se estiver RUNNING) */
  handle_io_requests_from_ready();   /* pega os READY pendentes */

  if (current_index < 0) {
    int nxt = pick_next_ready();
    if (nxt >= 0) { dispatch_index(nxt); time_slice_left = time_slice_seconds; }
    return;
  }

  if (time_slice_left > 0) time_slice_left--;
  if (time_slice_left == 0) {
    preempt_current();
    int nxt = pick_next_ready();
    if (nxt >= 0) { dispatch_index(nxt); time_slice_left = time_slice_seconds; }
  }
}

/* IRQ1: término de I/O — dá prioridade, mas sem “preempção relâmpago” em sequência */
static void handle_irq1_signal(int sig) {
  (void)sig;

  int idx = io_queue_pop();
  if (idx < 0) {
    printf("[KRL] IRQ1 but queue empty\n"); fflush(stdout);
    return;
  }

  printf("[KRL] IRQ1 -> READY idx=%d pid=%d\n", idx, (int)proc_pids[idx]); fflush(stdout);
  set_state(idx, ST_READY);

  /* Se já temos alguém rodando e ele foi despachado pelo último IRQ1,
     NÃO preemptamos agora: deixamos o recém-rodando executar o handler de SIGCONT. */
  if (current_index >= 0 && proc_state[current_index] == ST_RUNNING &&
      current_index == last_irq1_dispatch) {
    /* apenas deixa 'idx' em READY para pegar CPU no próximo tick */
    return;
  }

  /* Caso contrário, priorizamos o desbloqueado agora */
  if (current_index >= 0 && proc_state[current_index] == ST_RUNNING) {
    preempt_current();  /* coloca o atual em READY */
  }

  dispatch_index(idx);            /* manda o desbloqueado rodar agora */
  time_slice_left = time_slice_seconds;
  last_irq1_dispatch = idx;       /* marca que este foi promovido por IRQ1 */

  /* MICRO-RESPIRO: deixa o processo retomado executar o handler de SIGCONT
     antes de qualquer novo IRQ invadir. */
  struct timespec ts;
  ts.tv_sec  = 0;
  ts.tv_nsec = 5 * 1000 * 1000;   /* 5 ms */
  nanosleep(&ts, NULL);

}


/* sobe inter_controller com <kernel_pid> e <shm_id> */
static pid_t spawn_inter_controller(pid_t kernel_pid) {
  pid_t ic_pid = fork();
  if (ic_pid == 0) {
    char pid_str[16], shm_str[16];
    char *av[4];
    int_to_str10((int)kernel_pid, pid_str);
    int_to_str10((int)shm_id,   shm_str);
    av[0] = (char *)"inter_controller";
    av[1] = pid_str;
    av[2] = shm_str;
    av[3] = NULL;
    execv("./inter_controller", av);
    _exit(127);
  }
  return ic_pid;
}

/* parser */
static int parse_argv(int argc, char *argv[], char *cmds[][MAX_ARGS], int argc_per_cmd[]) {
  if (argc < 5) {
    printf("Uso:\n"
           "  %s <quantum_s> <duracao_s> -- <cmd1> [args...] -- <cmd2> [args...] ...\n"
           "Ex.: %s 1 15 -- ./app -- ./app argX\n",
           argv[0], argv[0]);
    return 1;
  }
  time_slice_seconds   = atoi(argv[1]);
  run_duration_seconds = atoi(argv[2]);
  if (time_slice_seconds <= 0 || run_duration_seconds <= 0) {
    printf("ERROR: quantum e duracao devem ser > 0\n"); return 1;
  }

  num_procs = 0;
  int i = 3;
  while (i < argc && num_procs < MAX_PROCS) {
    if (strcmp(argv[i], "--") == 0) {
      i++; if (i >= argc) break;
      int a = 0;
      while (i < argc && strcmp(argv[i], "--") != 0 && a < MAX_ARGS - 1) {
        cmds[num_procs][a++] = argv[i++];
      }
      cmds[num_procs][a] = NULL;
      argc_per_cmd[num_procs] = a;
      if (a > 0) num_procs++;
    } else {
      i++;
    }
  }
  if (num_procs < 1) { printf("Nenhum processo informado. Use -- para separar comandos.\n"); return 1; }
  return 0;
}

/* ===== MAIN ===== */
int main(int argc, char *argv[]) {
  char *cmd_argvs[MAX_PROCS][MAX_ARGS];
  int    cmd_argc [MAX_PROCS];
  if (parse_argv(argc, argv, cmd_argvs, cmd_argc)) return 1;

  /* sigaction persistente */
  struct sigaction sa0, sa1, sa_ign;
  memset(&sa0, 0, sizeof(sa0));
  sa0.sa_handler = handle_irq0_signal;
  sigemptyset(&sa0.sa_mask);
  sa0.sa_flags = SA_RESTART;
  sigaction(IRQ0_SIG, &sa0, NULL);

  memset(&sa1, 0, sizeof(sa1));
  sa1.sa_handler = handle_irq1_signal;
  sigemptyset(&sa1.sa_mask);
  sa1.sa_flags = SA_RESTART;
  sigaction(IRQ1_SIG, &sa1, NULL);

  memset(&sa_ign, 0, sizeof(sa_ign));
  sa_ign.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa_ign, NULL);

  /* SHM e FIFO */
  shared_memory_init(num_procs);
  mkfifo(FIFO_PATH, 0666);
  /* O_RDWR evita erro se o leitor ainda não abriu */
  fifo_fd = open(FIFO_PATH, O_RDWR | O_NONBLOCK);
  printf("[KRL] FIFO initial open fd=%d (errno=%d)\n", fifo_fd, errno); fflush(stdout);

  pid_t inter_controller_pid = spawn_inter_controller(getpid());
  printf("[kernel] shmid=%d\n", shm_id); fflush(stdout);

  /* cria filhos e injeta shm_id em argv[1] */
  for (int i = 0; i < num_procs; i++) {
    proc_pids[i] = fork();
    if (proc_pids[i] == 0) {
      char shm_str[16]; int_to_str10(shm_id, shm_str);
      char *child_argv[MAX_ARGS];
      int pos = 0;
      child_argv[pos++] = cmd_argvs[i][0];
      child_argv[pos++] = shm_str;
      for (int a = 1; cmd_argvs[i][a] != NULL && pos < MAX_ARGS-1; a++) {
        child_argv[pos++] = cmd_argvs[i][a];
      }
      child_argv[pos] = NULL;
      execv(child_argv[0], child_argv);
      _exit(127);
    }
    kill(proc_pids[i], SIGSTOP);
    shm->app_pid[i] = proc_pids[i];
    set_state(i, ST_READY);
  }

  int first = pick_next_ready();
  if (first >= 0) { dispatch_index(first); time_slice_left = time_slice_seconds; }

  printf("Kernel RR+IO: quantum=%ds, duracao=%ds, procs=%d\n",
         time_slice_seconds, run_duration_seconds, num_procs);
  fflush(stdout);

  int elapsed = 0;
  while (elapsed < run_duration_seconds) {
    sleep(1); elapsed++;
    /* varredura extra para READY entre ticks */
    handle_io_requests_from_ready();

    int vivos = 0;
    for (int i = 0; i < num_procs; i++) if (proc_state[i] != ST_DONE) vivos++;
    if (vivos == 0) { printf("Todos os processos terminaram. Encerrando antes do tempo.\n"); break; }
  }

  printf("Tempo encerrado. Finalizando processos...\n");
  for (int i = 0; i < num_procs; i++) if (proc_state[i] != ST_DONE) kill(proc_pids[i], SIGTERM);
  sleep(1);
  for (int i = 0; i < num_procs; i++) if (proc_state[i] != ST_DONE) kill(proc_pids[i], SIGKILL);
  for (int i = 0; i < num_procs; i++) waitpid(proc_pids[i], NULL, 0);

  if (inter_controller_pid > 0) kill(inter_controller_pid, SIGTERM);
  if (fifo_fd >= 0) { close(fifo_fd); fifo_fd = -1; }

  if (shm) {
    shm->done = 1;
    shmdt((void*)shm);
    shmctl(shm_id, IPC_RMID, NULL);
    shm = NULL; shm_id = -1;
  }
  printf("Kernel encerrado.\n");
  return 0;
}