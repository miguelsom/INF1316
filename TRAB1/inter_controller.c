/**
 * @file    inter_controller.c
 * @brief   Simula o controlador de interrupções do sistema.
 * @details Este processo envia sinais de interrupção (IRQ0 e IRQ1) ao kernel:
 *          - IRQ0 (SIGUSR1) a cada 1s, simulando o clock (time-slice).
 *          - IRQ1 (SIGUSR2) após 3s de um pedido de I/O recebido via FIFO.
 * 
 *          Ele lê pedidos de I/O do FIFO, atende um por vez (simulando 3s de serviço),
 *          e sinaliza o kernel quando o I/O termina.
 *
 *          Uso:
 *          ./inter_controller <shm_id> <kernel_pid>
 *          
 * 
 * @note    Trabalho 1 - INF1316 (Sistemas Operacionais)
 * @authors
 *          Miguel Mendes (2111705)
 *          Igor Lemos (2011287)
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

/**
 * @brief  Retorna o tempo atual em milissegundos desde o boot do sistema.
 * @return Tempo em milissegundos.
 */
static unsigned long tempo_ms(void){
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

/**
 * @brief  Retorna o tempo relativo em milissegundos desde um tempo inicial t0.
 * @param  t0 Tempo inicial em milissegundos.
 * @return Diferença (tempo atual - t0) em milissegundos.
 */
static unsigned long rel_ms(unsigned long t0){
  return tempo_ms() - t0;
}

/**
 * @struct shm_data
 * @brief  Estrutura compartilhada com o kernel e as aplicações.
 * @details Reflete o estado global da simulação de I/O e processos.
 */
struct shm_data {
  int  nprocs;               /**< Número total de processos */
  pid_t app_pid[6];          /**< PIDs das aplicações */
  int  pc[6];                /**< Contadores de programa */
  int  want_io[6];           /**< Flags de pedido de I/O */
  int  io_type[6];           /**< Tipo de I/O (0=READ, 1=WRITE) */
  int  done;                 /**< Flag de término global */
  int  d1_busy;              /**< Indica se o dispositivo está ocupado */
  pid_t io_inflight_pid;     /**< PID do processo em atendimento de I/O */
  pid_t io_done_pid;         /**< PID do processo cujo I/O terminou */
  int   io_done_type;        /**< Tipo do I/O concluído */
};

/**
 * @brief  Processo principal do InterController.
 * @param  argc Número de argumentos.
 * @param  argv Argumentos passados pela linha de comando (<shm_id> <kernel_pid>).
 * @return 0 em sucesso, >0 em falha.
 * @details 
 *  - Lê pedidos de I/O do FIFO ("/tmp/so_trab1_iofifo").
 *  - Envia SIGUSR1 (IRQ0) ao kernel a cada 1s.
 *  - Envia SIGUSR2 (IRQ1) ao kernel 3s após cada pedido de I/O.
 *  - Controla fila de I/O e atualiza o estado na SHM.
 */
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

  // Estado interno do controlador
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

    // IRQ0 periódico (clock)
    if (t >= prox_irq0){
      kill(kpid, IRQ0_SIG);
      printf("[IC %ldms] TICK (IRQ0)\n", rel_ms(t0));
      fflush(stdout);
      while (t >= prox_irq0){
        prox_irq0 += MS_TIMESLICE;
      }
    }

    // Leitura do FIFO: cada linha representa "PID TIPO\n"
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

    // Inicia atendimento de I/O se dispositivo livre e fila não vazia
    if (!io_ativo && qh != qt){
      cur_pid  = q_pid[qh];
      cur_tipo = q_tipo[qh];
      io_ativo = true;
      prazo_irq1 = t + MS_IO;

      shm->d1_busy = 1;
      shm->io_inflight_pid = cur_pid;

      printf("[IC %ldms] ATENDIMENTO INICIADO (pid=%d I/O=%s) | t_serviço=3s\n",
             rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
      fflush(stdout);
    }

    // Conclusão do atendimento e envio de IRQ1
    if (io_ativo && t >= prazo_irq1){
      shm->d1_busy = 0;
      shm->io_inflight_pid = 0;
      shm->io_done_pid = cur_pid;
      shm->io_done_type = cur_tipo;

      printf("[IC %ldms] ATENDIMENTO CONCLUÍDO (pid=%d I/O=%s) -> IRQ1\n",
             rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
      fflush(stdout);
      kill(kpid, IRQ1_SIG);

      // Avança fila e inicia o próximo atendimento, se houver
      qh = (qh + 1) % 128;
      if (qh != qt){
        cur_pid = q_pid[qh];
        cur_tipo = q_tipo[qh];
        io_ativo = true;
        prazo_irq1 = t + MS_IO;

        shm->d1_busy = 1;
        shm->io_inflight_pid = cur_pid;

        printf("[IC %ldms] ATENDIMENTO INICIADO (pid=%d I/O=%s) | t_serviço=3s\n",
               rel_ms(t0), (int)cur_pid, cur_tipo==0?"READ":"WRITE");
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
