#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/*
5) Faça um programa que tenha um coordenador e dois filhos. Os filhos executam
(execvp) um programa que tenha um loop eterno. O pai coordena a execução dos
filhos realizando a preempção dos processos, executando um deles por 1 segundo,
interrompendo a sua execução e executando o outro por 1 segundo, interrompendo a
sua execução e assim sucessivamente. O processo pai fica então coordenando a
execução dos filhos, é, na verdade, um escalonador. Faça o processo pai executar
por 15 segundos e, ao final, ele mata os processos filhos e termina. Explique
como realizou a preempção, se o programa funcionou a contento e as dificuldades
encontradas.
  */
int main(int argc, char *argv[]) {
  pid_t p1, p2;

  p1 = fork();
  if (p1 == 0){
    execvp(argv[1], argv);
    exit(0);
  }
  p2 = fork();
  if (p2 == 0){
    execvp(argv[1], argv);
    exit(0);
  }

  kill(p1, SIGSTOP);
  kill(p2, SIGSTOP);

  printf("Começando o escalonador...\n");

  for (int i = 0; i < 15; i++) {
    if (i % 2 == 0) {
      kill(p1, SIGCONT);
      sleep(1);
      kill(p1, SIGSTOP);
    } else {
      kill(p2, SIGCONT);
      sleep(1);
      kill(p2, SIGSTOP);
    }
  }
  kill(p1, SIGKILL);
  kill(p2, SIGKILL);
  return 0;
}