// app.c — teste de RR + SHM + I/O (IRQ1) sem setenv
// - argv[1] = shm_id (inteiro)
// - Anexa à SHM, encontra idx pelo PID em shm->app_pid[]
// - Loop de 20 iterações: atualiza pc e pede I/O em pc=3 e pc=8
// - Conta retomadas (SIGCONT) e imprime PASS/FAIL

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>

#define MAX_PROCS 32

/* Estrutura da SHM (espelha o kernel) */
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

/* Contador de retomadas pela CPU (SIGCONT) */
static volatile sig_atomic_t resumes = 0;

/* Identificação do processo neste app */
static int idx = -1;
static struct shm_data *shm = NULL;
static pid_t me = 0;

/* Handler persistente de SIGCONT (conta retomadas) */
static void on_sigcont(int s) {
  (void)s;
  resumes++;
  printf("[APP pid=%d idx=%d] SIGCONT #%d\n", (int)me, idx, (int)resumes);
  fflush(stdout);
}

/* Sleep simples em milissegundos (para simular trabalho) */
static void msleep(int ms){
  struct timespec ts;
  ts.tv_sec  = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

int main(int argc, char **argv){
  me = getpid();

  /* Registro persistente do handler de SIGCONT */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_sigcont;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCONT, &sa, NULL);

  /* shm_id via argv[1] */
  if (argc < 2){
    fprintf(stderr, "[APP pid=%d] FAIL: uso: ./app <shm_id>\n", (int)me);
    return 2;
  }
  int shm_id = atoi(argv[1]);
  if (shm_id <= 0){
    fprintf(stderr, "[APP pid=%d] FAIL: shm_id invalido\n", (int)me);
    return 2;
  }

  /* Anexa à SHM fornecida pelo kernel */
  shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1){
    perror("shmat");
    fprintf(stderr, "[APP pid=%d] FAIL: nao anexou SHM\n", (int)me);
    return 2;
  }

  /* Descobre o índice deste processo na tabela do kernel */
  for (int tries = 0; tries < 50 && idx < 0; tries++){
    for (int i = 0; i < shm->nprocs; i++){
      if (shm->app_pid[i] == me){ idx = i; break; }
    }
    if (idx < 0) msleep(50);
  }
  if (idx < 0){
    fprintf(stderr, "[APP pid=%d] FAIL: nao achei meu PID na SHM\n", (int)me);
    shmdt((void*)shm);
    return 2;
  }

  printf("[APP pid=%d idx=%d] START\n", (int)me, idx);
  fflush(stdout);

  /* Trabalho “CPU-bound” com 2 pedidos de I/O */
  const int total_iters    = 20;
  const int io_reqs_meta   = 2;
  int       io_feitos      = 0;

  for (int i = 0; i < total_iters; i++){
    shm->pc[idx] = i;

    if (i == 3 || i == 8){
      shm->want_io[idx] = 1;  /* kernel observa isso e bloqueia em WAITING */
      io_feitos++;
      printf("[APP pid=%d idx=%d] IO_REQ at pc=%d\n", (int)me, idx, i);
      fflush(stdout);
    }

    msleep(180);  /* simula uso de CPU; deixa tempo pro kernel alternar */
  }

  /* Resultado do teste: 2 I/O e pelo menos 2 retomadas */
  printf("[APP pid=%d idx=%d] DONE (iters=%d, io_reqs=%d, resumes=%d)\n",
         (int)me, idx, total_iters, io_feitos, (int)resumes);
  if (io_feitos == io_reqs_meta && resumes >= 2){
    printf("[APP pid=%d idx=%d] TEST RESULT: PASS\n", (int)me, idx);
  } else {
    printf("[APP pid=%d idx=%d] TEST RESULT: FAIL (esperado io=2 e resumes>=2)\n",
           (int)me, idx);
  }
  fflush(stdout);

  if (shm && shm != (void*)-1) shmdt((void*)shm);
  return 0;
}
