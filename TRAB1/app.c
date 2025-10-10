#include <stdio.h>
#include <unistd.h>

/* App de exemplo. */

int TEMPO_EXECUÇÃO = 50; // Segundos

int main(int argc, char **argv) {
  // Não estou usando ainda então serve para evitar warnings
  (void)argc;
  (void)argv;
  pid_t pid = getpid();
  printf("APP começou a rodar ! pid=%d\n", pid);
  fflush(stdout);
  if (TEMPO_EXECUÇÃO >= 0) {
    for (int i = 0; i < TEMPO_EXECUÇÃO; i++) {
      pid = getpid();
      printf("[%d pid | %d segundos] PIZZA\n", pid, i);
      sleep(5);
    }
  } else {
    while (1) {
      sleep(1);
    }
  }
  return 0;
}
