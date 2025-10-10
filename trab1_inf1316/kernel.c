#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_PROCS 32
#define MAX_ARGS 64

/* Tick do inter_controller (IRQ0) */
#define IRQ0_SIG SIGUSR1

/* Estados do processo no Kernel */
#define ST_READY 1
#define ST_RUNNING 2
#define ST_DONE 4

/* --- Tabelas do escalonador --- */
static pid_t PROC_PIDS[MAX_PROCS];
static int PROC_STATE[MAX_PROCS];
static int NPROCS = 0;

static int QUANTUM = 0;  /* segundos por fatia */
static int DURATION = 0; /* duração total do experimento em segundos */

static int current = -1;   /* índice do processo em execução */
static int slice_left = 0; /* segundos restantes da fatia atual */

/* Preempção: STOP no RUNNING → volta para READY */
static void preempt(void) {
  if (current >= 0 && PROC_STATE[current] == ST_RUNNING) {
    kill(PROC_PIDS[current], SIGSTOP);
    PROC_STATE[current] = ST_READY;
    /* <<< NÃO zera mais 'current' aqui.
       Mantemos o índice do último RUNNING para o pick_next()
       começar do (current + 1) e alternar certinho. */
    /* current = -1;  <-- removido */
  }
}

/* Despacho: READY → RUNNING e CONT */
static void dispatch(int idx) {
  current = idx;
  PROC_STATE[idx] = ST_RUNNING;
  kill(PROC_PIDS[idx], SIGCONT);
}

/* Próximo READY em Round-Robin, a partir de (current+1) */
static int pick_next(void) {
  if (NPROCS == 0)
    return -1;
  int start = (current + 1 + NPROCS) % NPROCS;
  for (int i = 0; i < NPROCS; i++) {
    int idx = (start + i) % NPROCS;
    if (PROC_STATE[idx] == ST_READY)
      return idx;
  }
  return -1;
}

/* Handler do tick: conta quantum e troca quando zerar */
static void IRQ0_SIG_handler(int s) {
  (void)s;
  // printf("[tick]\n");
  // fflush(stdout);
  /* Coleta não bloqueante: remove quem terminou do rodízio */
  for (int i = 0; i < NPROCS; i++) {
    if (PROC_STATE[i] != ST_DONE) {
      int st;
      pid_t r = waitpid(PROC_PIDS[i], &st, WNOHANG);
      if (r == PROC_PIDS[i]) {
        PROC_STATE[i] = ST_DONE;
        kill(PROC_PIDS[i], SIGSTOP); /* idempotente */
        if (current == i)
          current = -1;
      }
    }
  }

  /* Se ninguém rodando, tenta despachar alguém */
  if (current < 0) {
    int n = pick_next();
    if (n >= 0) {
      dispatch(n);
      slice_left = QUANTUM;
    }
    return;
  }

  /* Se há alguém rodando, decrementa a fatia */
  if (slice_left > 0)
    slice_left--;
  if (slice_left == 0) {
    preempt();
    int n = pick_next();
    if (n >= 0) {
      dispatch(n);
      slice_left = QUANTUM;
    }
  }
}

/* int -> string */
static void itoa10(int x, char *buf) {
  char tmp[32];
  int i = 0, j = 0, neg = 0;
  if (x == 0) {
    buf[0] = '0';
    buf[1] = 0;
    return;
  }
  if (x < 0) {
    neg = 1;
    x = -x;
  }
  while (x > 0 && i < 31) {
    tmp[i++] = (char)('0' + (x % 10));
    x /= 10;
  }
  if (neg)
    tmp[i++] = '-';
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = 0;
}

/* Sobe o inter_controller com execv, passando o PID do kernel em argv[1] */
static pid_t spawn_inter_controller(pid_t kernel_pid) {
  pid_t inter_controller_pid = fork();
  if (inter_controller_pid == 0) {
    char kernel_pid_string[16];
    char *av[3];
    itoa10((int)kernel_pid, kernel_pid_string);
    av[0] = (char *)"inter_controller";
    av[1] = kernel_pid_string;
    av[2] = NULL;
    execv("./inter_controller", av);
    _exit(127); /* se der erro em execv */
  }
  return inter_controller_pid;
}

/* ---- ARGV ----
   (mesma lógica movida para uma função para deixar a main mais limpa) */
static int parse_argv(int argc, char *argv[], char *cmds[][MAX_ARGS],
                      int nargs[]) {
  if (argc < 5) {
    printf("Uso:\n"
           "  %s <quantum_s> <duracao_s> -- <cmd1> [args...] -- <cmd2> "
           "[args...] ...\n"
           "Ex.: %s 1 15 -- ./app -- ./app argX\n",
           argv[0], argv[0]);
    return 1;
  }

  QUANTUM = atoi(argv[1]);
  DURATION = atoi(argv[2]);
  if (QUANTUM <= 0 || DURATION <= 0) {
    printf("ERROR: quantum e duracao devem ser > 0\n");
    return 1;
  }

  /* Parse dos comandos em grupos "--" */
  NPROCS = 0;
  int i = 3;
  while (i < argc && NPROCS < MAX_PROCS) {
    if (strcmp(argv[i], "--") == 0) {
      i++;
      if (i >= argc)
        break;
      int a = 0;
      while (i < argc && strcmp(argv[i], "--") != 0 && a < MAX_ARGS - 1) {
        cmds[NPROCS][a++] = argv[i++];
      }
      cmds[NPROCS][a] = NULL;
      nargs[NPROCS] = a;
      if (a > 0)
        NPROCS++;
    } else {
      i++;
    }
  }
  if (NPROCS < 1) {
    printf("Nenhum processo informado. Use -- para separar comandos.\n");
    return 1;
  }
  return 0;
}

/****************************************************/
/*********************** MAIN ***********************/
/****************************************************/

int main(int argc, char *argv[]) {
  /* ---- ARGV ---- */
  char *cmds[MAX_PROCS][MAX_ARGS];
  int nargs[MAX_PROCS];
  if (parse_argv(argc, argv, cmds, nargs)) {
    return 1;
  }

  /* ---- Kernel RR ---- */

  /* Instala handler do tick do inter_controller */
  signal(IRQ0_SIG, IRQ0_SIG_handler);

  /* Sobe o inter_controller depois que o handler já está instalado */
  pid_t inter_controller_pid = spawn_inter_controller(getpid());

  /* 1) Cria todos os filhos e guarda diretamente em PROC_PIDS[i] */
  for (int i = 0; i < NPROCS; i++) {
    PROC_PIDS[i] = fork();
    if (PROC_PIDS[i] == 0) {
      execv(cmds[i][0], cmds[i]); /* filho: troca de imagem */
      _exit(127);                 /* se execv falhar */
    }
    /* Pai segue; sem tratamento de erro no fork(), como você pediu */

    /* 2) Pausa todos; marca estado READY aqui (didático e coeso) */
    kill(PROC_PIDS[i], SIGSTOP);
    PROC_STATE[i] = ST_READY;
  }

  /* despacho inicial explícito para começar o RR já controlado */
  {
    int n = pick_next();
    if (n >= 0) {
      dispatch(n);
      slice_left = QUANTUM;
    }
  }

  printf("Kernel RR: quantum=%ds, duracao=%ds, procs=%d\n", QUANTUM, DURATION,
         NPROCS);

  /* Loop principal apenas controla a duração total e encerra ao final */
  int elapsed = 0;
  while (elapsed < DURATION) {
    sleep(1);
    elapsed++;

    /* Encerramento antecipado se todos já terminaram */
    int vivos = 0;
    for (int i = 0; i < NPROCS; i++)
      if (PROC_STATE[i] != ST_DONE)
        vivos++;
    if (vivos == 0) {
      printf("Todos os processos terminaram. Encerrando antes do tempo.\n");
      break;
    }
  }

  /* Finalização: tenta terminar gentilmente e depois força */
  printf("Tempo encerrado. Finalizando processos...\n");
  for (int i = 0; i < NPROCS; i++)
    if (PROC_STATE[i] != ST_DONE)
      kill(PROC_PIDS[i], SIGTERM);
  sleep(1);
  for (int i = 0; i < NPROCS; i++)
    if (PROC_STATE[i] != ST_DONE)
      kill(PROC_PIDS[i], SIGKILL);
  for (int i = 0; i < NPROCS; i++)
    waitpid(PROC_PIDS[i], NULL, 0);

  if (inter_controller_pid > 0)
    kill(inter_controller_pid, SIGTERM);
  printf("Kernel RR encerrado.\n");
  return 0;
}
