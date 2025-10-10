#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* --------------------------------------------------------------------
   InterController (MVP)
   - Papel: agir como um "relógio" do sistema, gerando um tick periódico.
   - A cada 1 segundo envia SIGUSR1 para o processo Kernel (IRQ0).

   Entrada:
     inter_controller <KERNEL_PID>

   Saída/efeitos:
     - Envia SIGUSR1 (IRQ0) ao Kernel a cada 1s por tempo indeterminado.
   -------------------------------------------------------------------- */

#define IRQ0_SIG SIGUSR1
// "IRQ0" = tick que dispara preempção no Kernel

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("uso: inter_controller <KERNEL_PID>\n");
    return 1;
  }

  pid_t kernel_pid = (pid_t)atoi(argv[1]);

  while (1) {
    if (kernel_pid > 0) {
      kill(kernel_pid, IRQ0_SIG);
    }

    sleep(1);
  }

  return 0;
}
