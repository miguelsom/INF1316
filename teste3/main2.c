#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/*
4) Faça um programa que leia 2 números reais e imprima o resultado das 4
operações básicas sobre estes 2 números. Verifique o que acontece se o 2º.
número da entrada for 0 (zero). Capture o sinal de erro de floating point
(SIGFPE) e repita a experiência anterior. Faça o mesmo agora lendo e realizando
as operações com inteiros. Explique o que ocorreu.
*/
void floating_point_exception_handler(int sinal) {
  printf("Floating point exception\n");
  exit(0);
}
int main(int argc, char *argv[]) {
  signal(SIGFPE, floating_point_exception_handler);

  int tmp = 0;
  int num1 = atoi(argv[1]);
  int num2 = atoi(argv[2]);

  tmp = num1 + num2;
  printf("Soma: %d\n", tmp);
  tmp = num1 - num2;
  printf("Subtração: %d\n", tmp);
  tmp = num1 * num2;
  printf("Multiplicação: %d\n", tmp);
  tmp = num1 / num2;
  printf("Divisão: %d\n", tmp);

  return 0;
}
