
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#define EVER                                                                   \
  ;                                                                            \
  ;
void intHandler(int sinal);
void quitHandler(int sinal);
int main(void) {
  pid_t id;
  id = getpid();
  signal(SIGINT, intHandler);
  signal(SIGQUIT, quitHandler);
  printf("Ctrl-C desabilitado. Use Ctrl-\\ para terminar\n");
  sleep(1000);
  for (EVER)
    ;
}
void intHandler(int sinal) { printf("Você pressionou Ctrl-C (%d)\n", sinal); }
void quitHandler(int sinal) {
  printf("Terminando o processo...\n");
  exit(0);
}
