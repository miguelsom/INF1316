#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  pid_t filho = fork();
  if (filho == 0) {
    execvp(argv[1], argv);
    exit(0);
  }
  waitpid(filho, NULL, 0);
  execvp(argv[2], argv);
}