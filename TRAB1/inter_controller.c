/* inter_controller.c - Simula controlador de interrupções
    - Envia SIGUSR1 (IRQ0) ao kernel a cada 1s (time-slice).
    - Envia SIGUSR2 (IRQ1) ao kernel 3s após cada pedido de I/O no FIFO.

  Uso:
    ./inter_controller <shm_id> <kernel_pid>
*/

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/shm.h>

/* Retorna o tempo em ms desde que o sistema foi iniciado */
static unsigned long tempo_ms(void){
  struct timespec ts; // Struct do time.h, para guardar segundos e nanossegundos
  clock_gettime(CLOCK_MONOTONIC, &ts); // Preenche a struct com o tempo "monotônico" (desde o boot)
  return (ts.tv_sec * 1000) +(ts.tv_nsec / 1000000); // tv_sec é segundos, tv_nsec é nanossegundos
}

/* Retorna o tempo relativo em ms desde um t0 */
static unsigned long rel_ms(unsigned long t0){
  return tempo_ms() - t0;
}

/* Estrutura deve ser compatível com a do kernel */
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

int main(int argc, char **argv){
  if (argc < 3){
    fprintf(stderr, "Uso: %s <shm_id> <kernel_pid>\n", argv[0]);
    return 1;
  }

  int shm_id = atoi(argv[1]);
  pid_t kpid  = (pid_t)atoi(argv[2]);

  struct shm_data *shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1){
    perror("[IC] shmat");
    return 1;
  }

  const int IRQ0_SIG = SIGUSR1;
  const int IRQ1_SIG = SIGUSR2;
  const unsigned long MS_TIMESLICE = 1000;
  const unsigned long MS_IO = 3000;
  const char *FIFO_CAMINHO = "/tmp/so_trab1_iofifo";

  int file_fifo = open(FIFO_CAMINHO, O_RDONLY | O_NONBLOCK);
  if (file_fifo < 0){
    perror("[IC] não foi possível abrir FIFO. IRQ1 não será testável.\n");
  }

  unsigned long t0 = tempo_ms();
  printf("[IC %ldms] INÍCIO (kpid=%d)\n", rel_ms(t0), (int)kpid);
  fflush(stdout);

  /* Estado dos timers e fila */
  unsigned long prox_irq0 = tempo_ms() + MS_TIMESLICE;
  pid_t q_pid[128]; int q_tipo[128]; int qh = 0, qt = 0;
  bool io_ativo = false;
  unsigned long prazo_irq1 = 0;
  pid_t cur_pid = 0; int cur_tipo = 0;
  char buffer[256];

  while(true){
    if (shm->done){
      break;
    }

    sleep(1);
    unsigned long t = tempo_ms();

    /* IRQ0 periódico */
    if (t >= prox_irq0){
      kill(kpid, IRQ0_SIG);
      printf("[IC %ldms] TICK (IRQ0)\n", rel_ms(t0));
      fflush(stdout);
      while (t >= prox_irq0){
        prox_irq0 += MS_TIMESLICE;
      }
    }

    /* Leitura do FIFO: cada linha "PID TIPO\n" -> 1 pedido */
    if (file_fifo >= 0) {
      while(true){
        int n = read(file_fifo, buffer, sizeof(buffer) - 1);
        if (n > 0){
          buffer[n] = '\0';
          char *p = buffer;
          while (*p){
            int pidv=0, tp=0, consumido=0;
            if (sscanf(p, "%d %d%n", &pidv, &tp, &consumido) == 2){
              q_pid[qt] = (pid_t)pidv;
              q_tipo[qt] = tp;
              qt = (qt + 1) % 128;
              printf("[IC %ldms] FILA <- pid=%d I/O=%s\n",
                     rel_ms(t0), pidv, tp==0?"READ":"WRITE");
              fflush(stdout);
              p += consumido;
            } else {
              break;
            }
          }
          continue;
        }
        break;
      }
    }

    /* Inicia atendimento de I/O se livre e fila não vazia */
    if (!io_ativo && qh !=qt){
      cur_pid  = q_pid[qh];
      cur_tipo = q_tipo[qh];
      io_ativo = true;
      prazo_irq1 = t + MS_IO;

      shm->d1_busy = 1;
      shm->io_inflight_pid = cur_pid;

      printf("[IC %ldms] ATENDIMENTO INICIADO (pid=%d I/O=%s) | t_serviço=3s\n", rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
      fflush(stdout);
    }

    /* Conclusão do atendimento e IRQ1 */
    if (io_ativo && t >= prazo_irq1){
      shm->d1_busy = 0;
      shm->io_inflight_pid = 0;
      shm->io_done_pid = cur_pid;
      shm->io_done_type = cur_tipo;

      printf("[IC %ldms] ATENDIMENTO CONCLUÍDO (pid=%d I/O=%s) -> IRQ1\n", rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
      fflush(stdout);
      kill(kpid, IRQ1_SIG);

      /* Avança fila (Inicia próximo atendimento se houver mais pedidos) */
      qh = (qh + 1) % 128;
      if (qh!= qt){
        cur_pid = q_pid[qh];
        cur_tipo =q_tipo[qh];
        io_ativo = true;
        prazo_irq1 = t + MS_IO;

        shm->d1_busy = 1;
        shm->io_inflight_pid = cur_pid;

        printf("[IC %ldms] ATENDIMENTO INICIADO (pid=%d I/O=%s) | t_serviço=3s\n", rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
        fflush(stdout);
      } else {
        io_ativo = false;
      }
    }
  }

  if (file_fifo >= 0){
    close(file_fifo);
  }

  shmdt((void*)shm);
  printf("[IC %ldms] FIM\n", rel_ms(t0));
  fflush(stdout);

  return 0;
}