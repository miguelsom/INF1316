#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  pid_t filho, neto;
  int shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
  int *N = (int *)shmat(shmid, NULL, 0);
  *N = 0;

  filho = fork();
  if (filho == 0) {
    neto = fork();
    if (neto == 0) {
      for (int i = 0; i < 100; i++)
        *N += 5;
      exit(0);
    }
    waitpid(neto, NULL, 0);
    for (int i = 0; i < 100; i++)
      *N += 2;
    exit(0);
  }

  waitpid(filho, NULL, 0);

  for (int i = 0; i < 100; i++)
    *N += 1;

  return 0;
}