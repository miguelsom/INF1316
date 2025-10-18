// app_rw.c — processo que faz I/O em pc=3 (READ) e pc=8 (WRITE)

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>

struct shm_data {
  int  nprocs;
  pid_t app_pid[6];
  int  pc[6];
  int  want_io[6];
  int  io_type[6];
  int  done;
  int  d1_busy;
  pid_t io_inflight_pid;
  pid_t io_done_pid;
  int   io_done_type;
};

static volatile sig_atomic_t got_sigcont = 0;
static void on_sigcont(int sig){ (void)sig; got_sigcont = 1; }

int main(int argc, char **argv) {
  pid_t me = getpid();

  if (argc < 2) {
    fprintf(stderr, "[APP pid=%d] uso: ./app <shm_id>\n", (int)me);
    return 2;
  }
  int shm_id = atoi(argv[1]);
  struct shm_data *shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1) { perror("[APP] shmat"); return 1; }

  // Acha índice na SHM
  int idx = -1;
  for (int tries = 0; tries < 100 && idx < 0; tries++) {
    for (int i = 0; i < shm->nprocs; i++) {
      if (shm->app_pid[i] == me) { idx = i; break; }
    }
    if (idx < 0) {
      struct timespec ts = {0, 50 * 1000 * 1000}; // 50ms
      nanosleep(&ts, NULL);
    }
  }
  if (idx < 0){
    fprintf(stderr, "[APP pid=%d] FAIL: não achei meu idx na SHM\n",(int)me);
    shmdt((void*)shm);
    return 2;
  }

  // Handler de retomada
  struct sigaction sa;
  memset(&sa,0,sizeof(sa));
  sa.sa_handler=on_sigcont; sigemptyset(&sa.sa_mask); sa.sa_flags=SA_RESTART;
  sigaction(SIGCONT,&sa,NULL);

  // Estado local
  int i = 0;                     // PC local (espelhado à SHM)
  int total_iters = 20;          // 20 iterações como no enunciado
  int io_feitos = 0;             // pedidos feitos
  int resumes = 0;               // contagem de SIGCONT
  int next_io_type = 0;          // alterna READ(0)/WRITE(1)

  printf("[APP pid=%d idx=%d] INÍCIO\n", (int)me, idx);
  fflush(stdout);

  // Loop controlado por i (PC explícito). Em toda retomada, sincroniza com SHM.
  while (i < total_iters) {
    if (got_sigcont) {
      got_sigcont = 0;
      resumes++;
      // restaura PC lógico explicitamente:
      i = shm->pc[idx];
      printf("[APP pid=%d idx=%d] RETORNO (SIGCONT) -> restaura pc=%d\n",(int)me,idx,i);
      fflush(stdout);
    }

    // "executa instrução"
    shm->pc[idx] = i;

    // syscalls didáticas nos PCs 3 e 8
    if (i == 3 || i == 8) {
      shm->want_io[idx] = 1;
      shm->io_type[idx] = next_io_type;         // 0=READ, 1=WRITE
      next_io_type ^= 1;                         // alterna
      printf("[APP pid=%d idx=%d] SYSCALL I/O %s em pc=%d\n", (int)me, idx, shm->io_type[idx]==0?"READ":"WRITE", i);
      fflush(stdout);
      io_feitos++;
    }

    // dorme 1s como pede o enunciado
    sleep(1);

    // avança PC local e espelha
    i++;
    shm->pc[idx] = i;
  }

  printf("[APP pid=%d idx=%d] FIM (iters=%d, io_reqs=%d, resumes=%d)\n", (int)me, idx, total_iters, io_feitos, resumes);
  fflush(stdout);

  shmdt((void*)shm);
  
  return 0;
}
