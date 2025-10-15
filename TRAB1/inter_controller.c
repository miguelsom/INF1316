/* 
--------------------------------------------------------------------
  InterControllerSim
  - IRQ0 (SIGUSR1) é enviado periodicamente (time-slice).
  - IRQ1 (SIGUSR2) é enviado no fim de uma I/O simulada. Acontece quando uma linha é escrita no FIFO (cada linha = 1 pedido).

  Entrada:
  - inter_controller <KERNEL_PID>

  Saída/efeitos:
  - Envia SIGUSR1 (IRQ0) ao kernel a cada 1s por tempo indeterminado.
  - Envia SIGUSR2 (IRQ1) ao kernel 3s após cada linha escrita no FIFO, para simular término de operação de I/O.
--------------------------------------------------------------------
*/

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>

/* Retorna o tempo em ms desde que o sistema foi iniciado */
static unsigned long tempo_ms(void){
  struct timespec ts; // Struct do time.h, para guardar segundos e nanossegundos
  clock_gettime(CLOCK_MONOTONIC, &ts); // Preenche a struct com o tempo "monotônico" (desde o boot)
  unsigned long res = (ts.tv_sec * 1000) +(ts.tv_nsec / 1000000); // tv_sec é segundos, tv_nsec é nanossegundos
  return res;
}

int main(int argc, char **argv){
  if (argc < 2){
    return 1;
  }

  pid_t kernel_pid = (pid_t)atoi(argv[1]);
  const int IRQ0_SIG = SIGUSR1; // tick do timeslice
  const int IRQ1_SIG = SIGUSR2; // término de I/O
  const unsigned long MS_TIMESLICE = 1000; // 1s
  const unsigned long MS_IO= 3000; // 3s por I/O
  const char *FIFO_CAMINHO = "/tmp/inf1316_d1_fifo";

  /* tenta abrir FIFO somente para leitura, não bloqueante (se não existir, segue sem IRQ1 mesmo) */
  int file_fifo = open(FIFO_CAMINHO, O_RDONLY | O_NONBLOCK);
  if (file_fifo < 0){
    printf("[InterController] nao foi possivel abrir FIFO. IRQ1 nao sera testavel.\n");
  }
  //printf("[InterController] Kernel PID=%d | IRQ0(sig=%d) %ldms | IRQ1(sig=%d) +%ldms | FIFO=%s\n", kernel_pid, IRQ0_SIG, MS_TIMESLICE, IRQ1_SIG, MS_IO, FIFO_CAMINHO);
  //fflush(stdout);

  /* Estado dos timers */
  unsigned long prox_irq0 = tempo_ms() + MS_TIMESLICE;
  bool io_ativo = false;
  unsigned long prazo_irq1 = 0; // quando a I/O atual termina
  unsigned int fila_io = 0;

  char buffer[256];

  while (true){
    usleep(100000); // 100ms

    unsigned long t = tempo_ms();

    /* IRQ0 periódico */
    if (t >= prox_irq0){
      kill(kernel_pid, IRQ0_SIG);
      printf("[InterController] IRQ0 (tick)\n");
      fflush(stdout);
      /* calcula próximo tick (pode pular vários se o processo ficou parado) */
      while (t >= prox_irq0){
        prox_irq0 = prox_irq0 + MS_TIMESLICE;
      }
    }

    /* leitura do FIFO: cada '\n' é 1 novo pedido de I/O */
    if (file_fifo >= 0) {
      while (true){
        int n = read(file_fifo, buffer, sizeof(buffer));
        if (n > 0){
          for (int i = 0;i < n; i++) {
            if (buffer[i] == '\n'){
              if (!io_ativo) {
                io_ativo = true;
                prazo_irq1 = t + MS_IO;
                printf("[InterController] Pedido de I/O iniciado: IRQ1 em +%ldms\n", MS_IO);
              }else {
                fila_io = fila_io + 1;
                printf("[InterController] Pedido de I/O enfileirado: pendentes = %u\n", fila_io);
              }
              fflush(stdout);
            }
          }
          continue;
        }
        break;
      }
    }

    /* IRQ1 quando o I/O atual termina. Atende fila sequencialmente */
    if (io_ativo && t >= prazo_irq1){
      kill(kernel_pid, IRQ1_SIG);
      printf("[InterController] IRQ1 (I/O concluido)");
      if (fila_io > 0){
        fila_io = fila_io - 1;
        prazo_irq1= t + MS_IO; /* inicia proximo imediatamente */
        printf(" -> proximo em +%ldms (restantes=%u)\n", MS_IO, fila_io);
      } else {
        io_ativo = false;
        printf("\n");
      }
      fflush(stdout);
    }
  }

  if(file_fifo >= 0){
    close(file_fifo);
  }

  return 0;
}