// Ex2 – Concorrência com memória compartilhada: 10 processos passam ao mesmo
// tempo pelo vetor inteiro sem sincronização

#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  int tamanho = 10000;
  int processos = 10;
  int valor_inicial = 10;

  // Mem. compartilhada para o vetor e ponteiro
  int id_vetor = shmget(IPC_PRIVATE, sizeof(int) * tamanho, IPC_CREAT | 0600);
  int *vetor = (int *)shmat(id_vetor, 0, 0);

  // Criação e preenchimento do vetor na mem. compartilhada
  for (int i = 0; i < tamanho; i++)
    vetor[i] = valor_inicial;

  // Criação dos multi processos
  for (int i = 0; i < processos; i++) {
    if (fork() == 0) {
      for (int j = 0; j < tamanho; j++) {
        vetor[j] = vetor[j] * 2;
        vetor[j] = vetor[j] + 2;
      }
      _exit(0);
    }
  }

  // Código para evitar que tenha algum filho ainda processando
  for (int i = 0; i < processos; i++)
    wait(NULL);

  // Verificação Automática. Verificar se todas as posições do vetor são iguais.
  int base = vetor[0];
  int iguais = 1;
  for (int i = 1; i < tamanho; i++) {
    if (vetor[i] != base) {
      iguais = 0;
      break;
    }
  }
  if (iguais) {
    printf("Todas as posicoes são iguais. Valor final = %d\n", base);
  } else {
    printf("Valores são diferentes (concorrencia).\n");
    printf("Exemplos: a[0]=%d, a[5000]=%d, a[9999]=%d\n", vetor[0], vetor[5000],
           vetor[9999]);
  }

  // Libera a mem. compartilhada
  shmdt(vetor);
  shmctl(id_vetor, IPC_RMID, 0);
  return 0;
}
