#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>
/*
Ex1: Paralelismo

Criar um vetor a de 10.000 posições inicializado com valor 10. Criar 10
processos trabalhadores que utilizam áreas diferentes do vetor para multiplicar
cada elemento da sua parcela do vetor por 2 armazenando esse resultado no vetor
e para somar as posições do vetor retornando o resultado para um processo
coordenador que irá apresentar a soma de todas as parcelas recebidas dos
trabalhadores.

Obs: O 1º trabalhador irá atuar nas primeiras 1.000 posições, o 2º trabalhador
nas 1.000 posições seguintes e assim sucessivamente.
*/
int TAM = 10000;
int main() {
  int shmid_vector = shmget(IPC_PRIVATE, TAM * sizeof(int), IPC_CREAT | 0666);
  int *vector = (int *)shmat(shmid_vector, NULL, 0);
  for (int i = 0; i < TAM; i++)
    vector[i] = 10;

  int shmid_sum_vector =
      shmget(IPC_PRIVATE, 10 * sizeof(int), IPC_CREAT | 0666);
  int *parcial_sum_vector = (int *)shmat(shmid_sum_vector, NULL, 0);
  for (int i = 0; i < 10; i++)
    parcial_sum_vector[i] = 0;

  // Criar 10 processos trabalhadores
  pid_t pids[10];
  for (int i = 0; i < 10; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      for (int j = i * 1000; j < (i + 1) * 1000; j++) {
        vector[j] *= 2;
        parcial_sum_vector[i] += vector[j];
      }
      exit(0);
    }
  }

  // Esperar pelos processos trabalhadores
  for (int i = 0; i < 10; i++)
    waitpid(pids[i], NULL, 0);

  int sum = 0;
  for (int i = 0; i < 10; i++)
    sum += parcial_sum_vector[i];
  printf("Soma: %d\n", sum);
}