/**
 * @file    app_cpu.c
 * @brief   Aplicativo de teste de CPU pura (sem operações de I/O).
 * @details Processo que executa apenas instruções de CPU para testar preempção no kernel.
 *          Cada instância se comunica via memória compartilhada (SHM) para atualizar seu
 *          contador de programa (pc). Ao receber SIGCONT, o processo retoma de onde parou.
 * 
 * @note    Usado para testes de escalonamento Round-Robin puro no trabalho INF1316 - SO.
 * @author  Miguel Mendes (2111705)
 * @author  Igor Lemos (2011287)
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>

/**
 * @struct shm_data
 * @brief  Estrutura de dados compartilhada entre kernel e apps.
 * @details Armazena informações sobre todos os processos, seus estados e pedidos de I/O.
 */
struct shm_data {
  int  nprocs;              /**< Número total de processos ativos */
  pid_t app_pid[6];         /**< PIDs dos processos de aplicação */
  int  pc[6];               /**< Contadores de programa individuais */
  int  want_io[6];          /**< Flags de solicitação de I/O */
  int  io_type[6];          /**< Tipo de I/O solicitado */
  int  done;                /**< Flag de término global */
  int  d1_busy;             /**< Flag de ocupação do dispositivo 1 */
  pid_t io_inflight_pid;    /**< PID do processo atualmente em I/O */
  pid_t io_done_pid;        /**< PID do processo que concluiu I/O */
  int   io_done_type;       /**< Tipo de I/O finalizado */
};

/**
 * @brief  Manipulador de sinal SIGCONT.
 * @param  sig Número do sinal recebido (ignorado).
 * @note   Define a flag global `got_sigcont` para indicar retomada do processo.
 */
static volatile sig_atomic_t got_sigcont = 0;
static void on_sigcont(int sig){ (void)sig; got_sigcont = 1; }

/**
 * @brief  Processo principal de execução (CPU-bound).
 * @param  argc Número de argumentos (espera 2: executável + shm_id).
 * @param  argv Argumentos passados pela linha de comando.
 * @return 0 em sucesso, >0 em falha.
 * @details Anexa à SHM, identifica seu índice, registra handler de sinal,
 *          executa laço de CPU simulando processamento contínuo sem I/O.
 * @note   Atualiza o contador `pc` na SHM a cada iteração e imprime logs de retomada.
 */
int main(int argc, char **argv) {
  pid_t me = getpid();

  if (argc < 2) {
    fprintf(stderr, "[APP pid=%d] uso: ./app <shm_id>\n", (int)me);
    return 2;
  }

  int shm_id = atoi(argv[1]);
  struct shm_data *shm = (struct shm_data*)shmat(shm_id, NULL, 0);
  if (shm == (void*)-1) { 
    perror("[APP] shmat"); 
    return 1; 
  }

  // Localiza o índice correspondente a este processo na SHM
  int idx = -1;
  for (int tries = 0; tries < 100 && idx < 0; tries++) {
    for (int i = 0; i < shm->nprocs; i++) {
      if (shm->app_pid[i] == me) { 
        idx = i; 
        break; 
      }
    }
    if (idx < 0) {
      struct timespec ts = {0, 50 * 1000 * 1000}; // 50ms
      nanosleep(&ts, NULL);
    }
  }

  if (idx < 0){
    fprintf(stderr, "[APP pid=%d] FAIL: não achei meu idx na SHM\n",(int)me);
    shmdt((void*)shm);
    return 2;
  }

  // Registra handler de SIGCONT para retomada após preempção
  struct sigaction sa; 
  memset(&sa,0,sizeof(sa));
  sa.sa_handler=on_sigcont; 
  sigemptyset(&sa.sa_mask); 
  sa.sa_flags=SA_RESTART;
  sigaction(SIGCONT,&sa,NULL);

  // Estado local
  int i = 0, total_iters = 20, resumes = 0;

  printf("[APP pid=%d idx=%d] INÍCIO (Apenas CPU)\n", (int)me, idx);
  fflush(stdout);

  // Loop principal de CPU (sem I/O)
  while (i < total_iters) {
    if (got_sigcont) {
      got_sigcont = 0;
      resumes++;
      i = shm->pc[idx];
      printf("[APP pid=%d idx=%d] RETORNO (SIGCONT) -> restaura pc=%d\n",(int)me,idx,i);
      fflush(stdout);
    }

    shm->pc[idx] = i;
    sleep(1);   // Simula carga de CPU
    i++;
    shm->pc[idx] = i;
  }

  printf("[APP pid=%d idx=%d] FIM (iters=%d, resumes=%d)\n", (int)me, idx, total_iters, resumes);
  fflush(stdout);

  shmdt((void*)shm);

  return 0;
}
