#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/time.h> // <<< necessário para gettimeofday
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {

  int segundos = 0;
  struct timeval t;

  int shmid = shmget(IPC_PRIVATE, sizeof(int) * 3, IPC_CREAT | 0666);
  int *vetor = (int *)shmat(shmid, NULL, 0);
  for (int i = 0; i < 3; i++)
    vetor[i] = 0;

  pid_t p1 = fork();
  if (p1 == 0) {
    while (1) {
      gettimeofday(&t, NULL);
      segundos = t.tv_sec % 60;
      if (segundos == 5) {
        for (int j = 0; j < 20; j++) {
          sleep(1);
          vetor[0]++;
        }
      } else {
        usleep(100000); // 100ms para não ocupar 100% CPU fora da janela
      }
    }
  }

  pid_t p2 = fork();
  if (p2 == 0) {
    while (1) {
      gettimeofday(&t, NULL);
      segundos = t.tv_sec % 60;
      if (segundos == 35) {
        for (int j = 0; j < 15; j++) {
          sleep(1);
          vetor[1]++;
        }
      } else {
        usleep(100000);
      }
    }
  }

  pid_t p3 = fork();
  if (p3 == 0) {
    while (1) {
      gettimeofday(&t, NULL);
      segundos = t.tv_sec % 60;
      // Use lógico (||, &&) em vez de bitwise (|, &):
      if ((segundos < 5) || (segundos > 50) ||
          ((segundos > 25) && (segundos < 35))) {
        sleep(1);
        vetor[2]++;
      } else {
        usleep(100000);
      }
    }
  }

  // --- Pai: deixa rodar por ~70s, depois encerra e coleta ---
  sleep(70); // dê tempo para ver 1 ciclo completo com folga

  kill(p1, SIGTERM);
  kill(p2, SIGTERM);
  kill(p3, SIGTERM);

  waitpid(p1, NULL, 0);
  waitpid(p2, NULL, 0);
  waitpid(p3, NULL, 0);

  printf("Resultados: v1=%d v2=%d v3=%d\n", vetor[0], vetor[1], vetor[2]);

  // limpa SHM
  shmdt(vetor);
  shmctl(shmid, IPC_RMID, NULL);
  return 0;
}
