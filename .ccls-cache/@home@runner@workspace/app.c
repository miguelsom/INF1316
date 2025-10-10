#include <stdio.h>
#include <unistd.h>

/* App de exemplo: apenas "trabalha" (sleep) continuamente.
   O Kernel controla quando ele roda (SIGCONT) e quando para (SIGSTOP). */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    while (1) {
        /* Coloque prints se quiser ver atividade, mas não é necessário. */
        sleep(1);
    }
    return 0;
}
