/*
  inter_controller.c
  - Envia IRQ0 (SIGUSR1) a cada 1s (tick).
  - Simula D1: cada linha recebida no FIFO "/tmp/inf1316_d1_fifo"
    agenda um IRQ1 (SIGUSR2) para +3s. Atende pedidos em série.
  - Aceita <KERNEL_PID> e opcionalmente <SHM_ID> (apenas para sair quando kernel->done).

  Uso:
    ./inter_controller <KERNEL_PID> [SHM_ID]
*/

#define _XOPEN_SOURCE 700
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define FIFO_PATH "/tmp/inf1316_d1_fifo"
#define MS_TICK   1000UL
#define MS_IO     3000UL

/* dorme em milissegundos usando nanosleep (portável) */
static void sleep_ms(unsigned long ms){
  struct timespec ts;
  ts.tv_sec  = (time_t)(ms / 1000UL);
  ts.tv_nsec = (long)((ms % 1000UL) * 1000000UL);
  nanosleep(&ts, NULL);
}

/* tempo em ms desde boot (monotônico) */
static unsigned long tempo_ms(void){
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

/* Estrutura SHM (apenas para ler 'done') — deve bater com a do kernel */
#define MAX_PROCS 32
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

int main(int argc, char **argv){
  if (argc < 2){
    fprintf(stderr, "Uso: %s <KERNEL_PID> [SHM_ID]\n", argv[0]);
    return 1;
  }

  pid_t kernel_pid = (pid_t)atoi(argv[1]);
  int shm_id = -1;
  if (argc >= 3) shm_id = atoi(argv[2]);

  /* Anexa à SHM se informado (apenas leitura para detectar término) */
  struct shm_data *shm = NULL;
  if (shm_id > 0) {
    shm = (struct shm_data*)shmat(shm_id, NULL, 0);
    if (shm == (void*)-1) shm = NULL;
  }

  /* Prepara FIFO */
  mkfifo(FIFO_PATH, 0666);
  int fd_fifo = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
  if (fd_fifo < 0){
    printf("[IC] aviso: nao consegui abrir FIFO para leitura (%s)\n", FIFO_PATH);
    fflush(stdout);
  }

  unsigned long prox_tick = tempo_ms() + MS_TICK;

  int fila_io = 0;               /* pedidos enfileirados além do atual */
  int io_ativo = 0;              /* 0=ocioso, 1=processando */
  unsigned long prazo_irq1 = 0;  /* em ms */

  char buf[256];

  printf("[IC] start: KPID=%d | tick=1s | io=3s | fifo=%s\n",
         (int)kernel_pid, FIFO_PATH);
  fflush(stdout);

  for (;;){
    /* “idle” curto para não ocupar CPU */
    sleep_ms(100);

    unsigned long t = tempo_ms();

    /* IRQ0 periódico (tick de 1s) */
    if (t >= prox_tick){
      kill(kernel_pid, SIGUSR1);
      while (t >= prox_tick) prox_tick += MS_TICK;
    }

    /* Ler FIFO: cada '\n' é 1 pedido de I/O */
    if (fd_fifo >= 0){
      for (;;) {
        int n = read(fd_fifo, buf, (int)sizeof(buf));
        if (n > 0){
          for (int i = 0; i < n; i++){
            if (buf[i] == '\n'){
              if (!io_ativo){
                io_ativo = 1;
                prazo_irq1 = t + MS_IO;
                printf("[IC] IO start -> IRQ1 em +%lums\n", MS_IO);
              } else {
                fila_io++;
                printf("[IC] IO enfileirado (pendentes=%d)\n", fila_io);
              }
              fflush(stdout);
            }
          }
          continue; /* tenta ler mais sem bloquear */
        }
        break; /* nada a ler agora */
      }
    }

    /* IRQ1 ao concluir I/O; encadeia próximo se houver */
    if (io_ativo && t >= prazo_irq1){
      kill(kernel_pid, SIGUSR2);
      if (fila_io > 0){
        fila_io--;
        prazo_irq1 = t + MS_IO;
        printf("[IC] IRQ1 -> proximo em +%lums (restantes=%d)\n", MS_IO, fila_io);
      } else {
        io_ativo = 0;
        printf("[IC] IRQ1 -> fila vazia\n");
      }
      fflush(stdout);
    }

    /* Encerramento gracioso se kernel marcou done */
    if (shm && shm->done) break;
  }

  if (fd_fifo >= 0) close(fd_fifo);
  if (shm) shmdt((void*)shm);
  return 0;
}
