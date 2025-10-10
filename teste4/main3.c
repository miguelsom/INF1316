#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Uso: %s <prog1> <prog2>\n", argv[0]);
    return 1;
  }

  pid_t p1 = fork();
  if (p1 == 0) {
    raise(SIGSTOP); // garante iniciar parado
    execvp(argv[1], &argv[1]);
    _exit(127);
  }

  pid_t p2 = fork();
  if (p2 == 0) {
    raise(SIGSTOP); // garante iniciar parado
    execvp(argv[2], &argv[2]);
    _exit(127);
  }

  // (opcional/redundante) garantir parados
  kill(p1, SIGSTOP);
  kill(p2, SIGSTOP);

  printf("Rodando escalonador...\n");
  int segundos = 0;
  for (int i = 0; i < 10; i++) {
    if (i % 2 == 0) {
      kill(p1, SIGSTOP);
      kill(p2, SIGCONT);
      printf("z\n");
    } else {
      kill(p2, SIGSTOP);
      kill(p1, SIGCONT);
      printf("y\n");
    }
    sleep(1);
  }

  // encerra limpo: matar primeiro, depois esperar
  kill(p1, SIGTERM);
  kill(p2, SIGTERM);
  waitpid(p1, NULL, 0);
  waitpid(p2, NULL, 0);

  return 0;
}
