// inter_controller.c

#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define FIFO_PATH "/tmp/so_trab1_iofifo"

// ===== util de tempo (ms relativos) =====
static long t0_ms = -1;
static long now_ms(void){
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec*1000L + ts.tv_nsec/1000000L;
}
static void init_t0(void){ if (t0_ms < 0) t0_ms = now_ms(); }
static long rel_ms(void){ return now_ms() - t0_ms; }
// =======================================

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

static volatile sig_atomic_t stop_flag = 0;
static void on_stop(int sig){ (void)sig; stop_flag = 1; }

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "[IC] uso: ./inter_controller <shm_id> <kernel_pid>\n");
    return 2;
  }
  int shm_id = atoi(argv[1]);
  pid_t kpid  = (pid_t)atoi(argv[2]);

  init_t0();

  struct sigaction sa;
  memset(&sa,0,sizeof(sa));
  sa.sa_handler = on_stop; sigemptyset(&sa.sa_mask); sa.sa_flags = SA_RESTART;
  sigaction(SIGINT,&sa,NULL); sigaction(SIGTERM,&sa,NULL);

  struct shm_data *shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1) { perror("[IC] shmat"); return 1; }

  int fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
  if (fd < 0) { perror("[IC] open fifo rd"); return 1; }

  printf("[IC %ldms] INÍCIO (kpid=%d)\n", rel_ms(), (int)kpid); fflush(stdout);

  pid_t q_pid[64]; int q_type[64]; int qh=0, qt=0;
  int busy = 0;
  time_t end_time = 0;
  pid_t cur_pid = 0; int cur_type = 0;

  while (1) {
    if (stop_flag) break;

    // Tick de 1s
    kill(kpid, SIGUSR1);
    printf("[IC %ldms] TICK (IRQ0)\n", rel_ms()); fflush(stdout);

    // Ler FIFO
    char buf[128];
    ssize_t r = read(fd, buf, sizeof(buf)-1);
    if (r > 0) {
      buf[r] = 0;
      char *p = buf;
      while (*p) {
        int pidv=0, tp=0, n=0;
        if (sscanf(p, "%d %d%n", &pidv, &tp, &n) == 2) {
          q_pid[qt] = (pid_t)pidv; q_type[qt] = tp; qt = (qt + 1) % 64;
          printf("[IC %ldms] FILA <- pid=%d I/O=%s\n",
                 rel_ms(), pidv, tp==0?"READ":"WRITE");
          fflush(stdout);
          p += n;
        } else { break; }
      }
    }

    // Inicia próximo atendimento se não ocupado
    if (!busy && qh != qt) {
      cur_pid = q_pid[qh];
      cur_type = q_type[qh];
      busy = 1;
      end_time = time(NULL) + 3;

      // marca "em serviço"
      shm->d1_busy = 1;
      shm->io_inflight_pid = cur_pid;

      printf("[IC %ldms] ATENDIMENTO INICIADO (pid=%d I/O=%s) | t_serviço=3s\n",
             rel_ms(), (int)cur_pid, cur_type==0?"READ":"WRITE");
      fflush(stdout);
    }

    // Se terminou o atendimento atual
    if (busy && time(NULL) >= end_time) {
      busy = 0;
      // consome da fila
      (void)q_pid[qh]; (void)q_type[qh];
      qh = (qh + 1) % 64;

      // marca "concluído"
      shm->d1_busy = 0;
      shm->io_inflight_pid = 0;
      shm->io_done_pid = cur_pid;
      shm->io_done_type = cur_type;

      printf("[IC %ldms] ATENDIMENTO CONCLUÍDO (pid=%d I/O=%s) -> IRQ1\n",
             rel_ms(), (int)cur_pid, cur_type==0?"READ":"WRITE");
      fflush(stdout);
      kill(kpid, SIGUSR2);
    }

    if (shm->done) break;
    sleep(1);
  }

  close(fd);
  shmdt((void*)shm);
  printf("[IC %ldms] FIM\n", rel_ms());
  return 0;
}
