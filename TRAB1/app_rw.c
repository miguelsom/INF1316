/**
 * @file    app_rw.c
 * @brief   Aplicativo que realiza operações de I/O simuladas em momentos específicos.
 * @details Processo de aplicação que solicita I/O em instruções definidas (pc=3 e pc=8),
 *          alternando entre READ e WRITE. Ele se comunica com o kernel via memória
 *          compartilhada (SHM) para indicar seu estado e registrar pedidos de I/O.
 * 
 *          Usado para testar o comportamento do escalonador Round-Robin com bloqueios
 *          e retomadas de execução (SIGSTOP/SIGCONT) no Trabalho 1 de INF1316.
 * 
 * @note    Trabalho 1 - INF1316 (Sistemas Operacionais)
 * @authors
 *          Miguel Mendes (2111705)
 *          Igor Lemos (2011287)
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
 * @brief  Estrutura compartilhada entre Kernel, InterController e APPs.
 * @details Contém o estado de todos os processos, controle de I/O e variáveis de sincronização.
 */
struct shm_data {
  int  nprocs;              /**< Número total de processos ativos */
  pid_t app_pid[6];         /**< PIDs das aplicações */
  int  pc[6];               /**< Contadores de programa (PC) */
  int  want_io[6];          /**< Flags de solicitação de I/O */
  int  io_type[6];          /**< Tipo de I/O solicitado (0=READ, 1=WRITE) */
  int  done;                /**< Flag global de término */
  int  d1_busy;             /**< Indica se o dispositivo de I/O está ocupado */
  pid_t io_inflight_pid;    /**< PID do processo atualmente em atendimento de I/O */
  pid_t io_done_pid;        /**< PID do processo cujo I/O terminou */
  int   io_done_type;       /**< Tipo do I/O concluído (0=READ, 1=WRITE) */
};

/**
 * @brief  Flag global que indica retomada do processo via SIGCONT.
 */
static volatile sig_atomic_t got_sigcont = 0;

/**
 * @brief  Manipulador de sinal SIGCONT.
 * @param  sig Número do sinal recebido (não utilizado).
 * @note   Apenas marca a flag `got_sigcont` para indicar retomada de execução.
 */
static void on_sigcont(int sig){ (void)sig; got_sigcont = 1; }

/**
 * @brief  Função principal do processo APP com operações de I/O.
 * @param  argc Número de argumentos (espera 2: executável + shm_id).
 * @param  argv Argumentos da linha de comando.
 * @return 0 em sucesso, >0 em falha.
 * @details 
 *  - Conecta-se à memória compartilhada criada pelo kernel.
 *  - Identifica seu índice (`idx`) dentro da SHM.
 *  - Executa um loop de 20 iterações simulando instruções de CPU.
 *  - Em `pc=3` e `pc=8`, solicita I/O alternando entre READ e WRITE.
 *  - Pausa e retoma execução conforme escalonador (SIGSTOP/SIGCONT).
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

  // Identifica o índice deste processo na memória compartilhada
  int idx = -1;
  for (int tries = 0; tries < 100 && idx < 0; tries++) {
    for (int i = 0; i < shm->nprocs; i++) {
      if (shm->app_pid[i] == me) { idx = i; break; }
    }
    if (idx < 0) {
      struct timespec ts = {0, 50 * 1000 * 1000}; // espera 50ms
      nanosleep(&ts, NULL);
    }
  }

  if (idx < 0){
    fprintf(stderr, "[APP pid=%d] FAIL: não achei meu idx na SHM\n",(int)me);
    shmdt((void*)shm);
    return 2;
  }

  // Configura o handler de retomada (SIGCONT)
  struct sigaction sa;
  memset(&sa,0,sizeof(sa));
  sa.sa_handler=on_sigcont; 
  sigemptyset(&sa.sa_mask); 
  sa.sa_flags=SA_RESTART;
  sigaction(SIGCONT,&sa,NULL);

  // Estado local do processo
  int i = 0;                     /**< contador de instruções (PC) */
  int total_iters = 20;          /**< número total de iterações */
  int io_feitos = 0;             /**< total de pedidos de I/O feitos */
  int resumes = 0;               /**< contador de retomadas via SIGCONT */
  int next_io_type = 0;          /**< alternância entre READ(0) e WRITE(1) */

  printf("[APP pid=%d idx=%d] INÍCIO\n", (int)me, idx);
  fflush(stdout);

  // Loop principal (simulação de execução)
  while (i < total_iters) {
    if (got_sigcont) {
      got_sigcont = 0;
      resumes++;
      i = shm->pc[idx];
      printf("[APP pid=%d idx=%d] RETORNO (SIGCONT) -> restaura pc=%d\n",(int)me,idx,i);
      fflush(stdout);
    }

    shm->pc[idx] = i;

    // Solicitação de I/O simulada nos PCs 3 e 8
    if (i == 3 || i == 8) {
      shm->want_io[idx] = 1;
      shm->io_type[idx] = next_io_type;   // define tipo de I/O
      next_io_type ^= 1;                  // alterna entre READ/WRITE
      printf("[APP pid=%d idx=%d] SYSCALL I/O %s em pc=%d\n",
             (int)me, idx, shm->io_type[idx]==0?"READ":"WRITE", i);
      fflush(stdout);
      io_feitos++;
    }

    sleep(1);  // Simula tempo de execução (1 segundo por instrução)

    i++;
    shm->pc[idx] = i;
  }

  printf("[APP pid=%d idx=%d] FIM (iters=%d, io_reqs=%d, resumes=%d)\n",
         (int)me, idx, total_iters, io_feitos, resumes);
  fflush(stdout);

  shmdt((void*)shm);

  return 0;
}
