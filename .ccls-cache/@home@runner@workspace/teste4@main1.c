#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int numero_inicial = 1;
int tamanho_vetor = 10;
int numero_multiplicar = 2;
int qtd_processos = 10;

int main() {
  int slice_vetor = tamanho_vetor / qtd_processos;
  pid_t pids[qtd_processos];
  int shmid =
      shmget(IPC_PRIVATE, tamanho_vetor * sizeof(int), IPC_CREAT | 0666);
  int *vetor = (int *)shmat(shmid, NULL, 0);

  for (int i = 0; i < tamanho_vetor; i++)
    vetor[i] = numero_inicial;
  for (int i = 0; i < qtd_processos; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      for (int j = i * slice_vetor; j < (i + 1) * slice_vetor; j++)
        vetor[j] *= numero_multiplicar;
      exit(0);
    }
  }
  for (int i = 0; i < qtd_processos; i++)
    waitpid(pids[i], NULL, 0);
  for (int i = 0; i < tamanho_vetor; i++)
    printf("%d ", vetor[i]);
  shmdt(vetor);
  shmctl(shmid, IPC_RMID, NULL);
  return 0;
}
