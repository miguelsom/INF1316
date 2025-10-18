// Integrantes do Grupo: Miguel Mendes (2111705) e Igor Lemos (2011287)
// app_cpu.c — processo sem I/O (só CPU): útil pra testar preempção pura

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
  struct sigaction sa; memset(&sa,0,sizeof(sa));
  sa.sa_handler=on_sigcont; sigemptyset(&sa.sa_mask); sa.sa_flags=SA_RESTART;
  sigaction(SIGCONT,&sa,NULL);

  // Estado local
  int i = 0, total_iters = 20, resumes = 0;

  printf("[APP pid=%d idx=%d] INÍCIO (Apenas CPU)\n", (int)me, idx);
  fflush(stdout);

  while (i < total_iters) {
    if (got_sigcont) {
      got_sigcont = 0;
      resumes++;
      i = shm->pc[idx];
      printf("[APP pid=%d idx=%d] RETORNO (SIGCONT) -> restaura pc=%d\n",(int)me,idx,i);
      fflush(stdout);
    }
    shm->pc[idx] = i;

    // Sem I/O: não seta want_io/io_type
    sleep(1);
    i++;
    shm->pc[idx] = i;
  }
  printf("[APP pid=%d idx=%d] FIM (iters=%d, resumes=%d)\n", (int)me, idx, total_iters, resumes);
  fflush(stdout);

  shmdt((void*)shm);
  
  return 0;
}