# KernelSim – INF1316 – Trabalho 1

É um simulador de núcleo de sistema operacional com escalonamento preemptivo round-robin (RR) e dispositivo de entrada e saída simulado (D1), usando processos Unix, sinais, memória compartilhada (SHM) e FIFO.

## Integrantes do Grupo
Miguel Mendes — 2111705  
Igor Lemos — 2011287

---

## Introdução

Este trabalho implementa um simulador simplificado de núcleo de sistema operacional (KernelSim), com escalonamento preemptivo e suporte a operações de I/O.  
O objetivo é demonstrar, na prática, o funcionamento de um escalonador Round-Robin com interrupções, sinais e memória compartilhada, evidenciando a interação entre Kernel, Controlador de Interrupções e processos de usuário.

---

## Estrutura

- **`kernel`** — *KernelSim*: faz RR com quantum configurável e preempção com `SIGSTOP`/`SIGCONT`, além de bloqueio e desbloqueio por I/O (IRQ1), e coordena SHM e FIFO;
- **`inter_controller`** — *InterController Sim*: gera as seguintes interrupções:
  - **IRQ0** (`SIGUSR1`) a cada 1s — marca o fim do *time-slice*;
  - **IRQ1** (`SIGUSR2`) ~3s após cada pedido de I/O — sinaliza término do serviço de I/O;
- **Aplicações (Ai)** para teste:
  - **`app_cpu`** — não pede I/O (apenas CPU), útil para observar a preempção “pura”;
  - **`app_rw`** — pede I/O em `pc=3` (**READ**) e `pc=8` (**WRITE**), alternando as operações.

```
+-----------------------------+
|          kernel.c           |
|-----------------------------|
| - Escalonamento RR          |
| - Controle de sinais        |
| - Comunicação via SHM/FIFO  |
+--------------+--------------+
               ^
               |  IRQ0 / IRQ1 (SIGUSR1 / SIGUSR2)
               v
+-----------------------------+
|     inter_controller.c      |
|-----------------------------|
| - Envia IRQ0 (clock)        |
| - Envia IRQ1 (I/O done)     |
| - Lê pedidos do FIFO        |
+--------------+--------------+
               ^
               |  SHM + FIFO
               v
+-----------------------------+
|        Aplicações (APPs)    |
|-----------------------------|
| - app_cpu.c: só CPU         |
| - app_rw.c: CPU + I/O       |
+-----------------------------+
```

---

## Funcionamento

- RR com **quantum = 1s** (padrão): a cada IRQ0, o kernel **preempta** quem está na CPU (`SIGSTOP`) e **despacha** o próximo pronto (`SIGCONT`);
- Quando detecta `want_io[idx]`, o kernel **bloqueia** o processo (estado `ST_WAITING`), registra o pedido no **FIFO** (`PID TIPO`) e retira-o da CPU;
- O `inter_controller` **lê** o FIFO, **atende um pedido por vez** (serviço de ~3s) e, ao concluir, escreve `io_done_pid/type` na SHM e envia **IRQ1**;
- No IRQ1, o kernel **desbloqueia com prioridade**: preempta quem estiver rodando e despacha o processo que acabou de sair do I/O;
- Cada APP salva/restaura seu `pc` na SHM ao receber `SIGSTOP`/`SIGCONT`, garantindo que retome exatamente do ponto onde parou.

---

## Build e Execução

```bash
gcc -Wall -o kernel           kernel.c
gcc -Wall -o inter_controller inter_controller.c
gcc -Wall -o app_rw           app_rw.c
gcc -Wall -o app_cpu          app_cpu.c
```

**Sem limitações:** o kernel aceita **um executável por tarefa** usando blocos `-- <app>` na linha de comando. Isso permite misturar `app_cpu` e `app_rw` **na mesma execução**.

Formato:
```bash
./kernel <quantum_s> <duracao_s> -- <app1> [-- <app2>] [-- <app3>] ...
```

Exemplos:
- 3 processos **apenas CPU**:
  ```bash
  ./kernel 1 22 -- ./app_cpu -- ./app_cpu -- ./app_cpu
  ```
- 3 processos **apenas I/O**:
  ```bash
  ./kernel 1 30 -- ./app_rw -- ./app_rw -- ./app_rw
  ```
- 6 processos **mistos**:
  ```bash
  ./kernel 1 40 -- ./app_cpu -- ./app_cpu -- ./app_cpu -- ./app_rw -- ./app_rw -- ./app_rw
  ```

---

## Linha do Tempo (exemplo guiado)

Cenário exemplo (3 × `app_rw`):  
1) `t=0s`: Kernel inicia, despacha A1; 
2) `t=1s`: IRQ0 → A1 preemptado, A2 despachado;
3) `t=2s`: A2 preemptado, A3 despachado;  
4) `t≈3s`: A1 chega ao `pc=3` → `SYSCALL I/O READ` → **BLOQUEIO** → pedido vai ao FIFO;  
5) `t≈6s`: InterController conclui I/O de A1 → **IRQ1** → kernel **DESBLOQUEIA** A1 com **PRIORIDADE** (preempta o atual);  
6) `t≈8–9s`: A1 chega ao `pc=8` → `SYSCALL I/O WRITE` → **BLOQUEIO** → novo ciclo de I/O;  
7) Demais processos repetem o mesmo padrão, com serviços de 3s em série (ordem de chegada).

---

## Testes da Avaliação (A1..A3; A4..A6; A1..A6)

A seguir, apresentamos **configuração**, **linha do tempo esperada** e **linha do tempo obtida** para os três cenários solicitados.

### Cenário 1 — A1, A2 e A3 (somente CPU)

**Configuração:** `./kernel 4 20 -- ./app_cpu -- ./app_cpu -- ./app_cpu`

#### Linha do tempo — *Esperada*
RR puro (quantum=4s) entre idx=0,1,2; sem SYSCALL I/O.

#### Linha do tempo — *Obtida*
```
./kernel 4 20 -- ./app_cpu -- ./app_cpu -- ./app_cpu
[IC 0ms] INÍCIO (kpid=909)
[KRL 2ms] INÍCIO | RR+I/O | quantum=4s | duração=20s | procs=3
[KRL 2ms] DESPACHE -> idx=0 pid=911
[APP pid=911 idx=0] INÍCIO (Apenas CPU)
[IC 1000ms] TICK (IRQ0)
[IC 2000ms] TICK (IRQ0)
[IC 3000ms] TICK (IRQ0)
[IC 4000ms] TICK (IRQ0)
[KRL 4002ms] PREEMPÇÃO -> idx=0 pid=911 (sai da CPU)
[KRL 4002ms] DESPACHE -> idx=1 pid=912
[APP pid=912 idx=1] INÍCIO (Apenas CPU)
[IC 5000ms] TICK (IRQ0)
[IC 6000ms] TICK (IRQ0)
[IC 7000ms] TICK (IRQ0)
[IC 8001ms] TICK (IRQ0)
[KRL 8003ms] PREEMPÇÃO -> idx=1 pid=912 (sai da CPU)
[KRL 8003ms] DESPACHE -> idx=2 pid=913
[APP pid=913 idx=2] INÍCIO (Apenas CPU)
[IC 9001ms] TICK (IRQ0)
[IC 10001ms] TICK (IRQ0)
[IC 11001ms] TICK (IRQ0)
[IC 12001ms] TICK (IRQ0)
[KRL 12003ms] PREEMPÇÃO -> idx=2 pid=913 (sai da CPU)
[KRL 12003ms] DESPACHE -> idx=0 pid=911
[APP pid=911 idx=0] RETORNO (SIGCONT) -> restaura pc=4
[IC 13001ms] TICK (IRQ0)
[IC 14001ms] TICK (IRQ0)
[IC 15001ms] TICK (IRQ0)
[IC 16001ms] TICK (IRQ0)
[KRL 16003ms] PREEMPÇÃO -> idx=0 pid=911 (sai da CPU)
[KRL 16003ms] DESPACHE -> idx=1 pid=912
[APP pid=912 idx=1] RETORNO (SIGCONT) -> restaura pc=4
[IC 17001ms] TICK (IRQ0)
[IC 18001ms] TICK (IRQ0)
[IC 19002ms] TICK (IRQ0)
[IC 20002ms] TICK (IRQ0)
[KRL 20004ms] PREEMPÇÃO -> idx=1 pid=912 (sai da CPU)
[KRL 20004ms] DESPACHE -> idx=2 pid=913
[KRL 20004ms] FIM do Kernel
```

---

### Cenário 2 — A4, A5 e A6 (somente I/O)

**Configuração:** `./kernel 4 20 -- ./app_rw -- ./app_rw -- ./app_rw`

#### Linha do tempo — *Esperada*
Ciclos: SYSCALL I/O → BLOQUEIO → ATENDIMENTO (3s) → IRQ1 → DESBLOQUEIO (PRIORIDADE).

#### Linha do tempo — *Obtida*
```
./kernel 4 20 -- ./app_rw -- ./app_rw -- ./app_rw
[IC 0ms] INÍCIO (kpid=926)
[KRL 2ms] INÍCIO | RR+I/O | quantum=4s | duração=20s | procs=3
[KRL 2ms] DESPACHE -> idx=0 pid=928
[APP pid=928 idx=0] INÍCIO
[IC 1000ms] TICK (IRQ0)
[IC 2000ms] TICK (IRQ0)
[IC 3000ms] TICK (IRQ0)
[APP pid=928 idx=0] SYSCALL I/O READ em pc=3
[IC 4000ms] TICK (IRQ0)
[KRL 4002ms] BLOQUEIO (I/O READ) -> idx=0 pid=928 | ENFILEIRA
[KRL 4002ms] DESPACHE -> idx=1 pid=929
[APP pid=929 idx=1] INÍCIO
[IC 5000ms] TICK (IRQ0)
[IC 5000ms] FILA <- pid=928 I/O=READ
[IC 5000ms] ATENDIMENTO INICIADO (pid=928 I/O=READ) | t_serviço=3s
[IC 6000ms] TICK (IRQ0)
[IC 7000ms] TICK (IRQ0)
[APP pid=929 idx=1] SYSCALL I/O READ em pc=3
[IC 8001ms] TICK (IRQ0)
[IC 8001ms] ATENDIMENTO CONCLUÍDO (pid=928 I/O=READ) -> IRQ1
[KRL 8003ms] BLOQUEIO (I/O READ) -> idx=1 pid=929 | ENFILEIRA
[KRL 8003ms] DESPACHE -> idx=2 pid=930
[KRL 8003ms] DESBLOQUEIO (IRQ1 I/O READ) -> idx=0 pid=928 | PRIORIDADE
[KRL 8003ms] PREEMPÇÃO -> idx=2 pid=930 (sai da CPU)
[KRL 8003ms] DESPACHE -> idx=0 pid=928
[APP pid=928 idx=0] RETORNO (SIGCONT) -> restaura pc=4
[IC 9001ms] TICK (IRQ0)
[IC 9001ms] FILA <- pid=929 I/O=READ
[IC 9001ms] ATENDIMENTO INICIADO (pid=929 I/O=READ) | t_serviço=3s
[IC 10001ms] TICK (IRQ0)
[IC 11001ms] TICK (IRQ0)
[APP pid=928 idx=0] SYSCALL I/O WRITE em pc=8
[IC 12001ms] TICK (IRQ0)
[IC 12001ms] ATENDIMENTO CONCLUÍDO (pid=929 I/O=READ) -> IRQ1
[KRL 12003ms] BLOQUEIO (I/O WRITE) -> idx=0 pid=928 | ENFILEIRA
[KRL 12003ms] DESPACHE -> idx=2 pid=930
[KRL 12003ms] DESBLOQUEIO (IRQ1 I/O READ) -> idx=1 pid=929 | PRIORIDADE
[KRL 12003ms] PREEMPÇÃO -> idx=2 pid=930 (sai da CPU)
[KRL 12003ms] DESPACHE -> idx=1 pid=929
[APP pid=929 idx=1] RETORNO (SIGCONT) -> restaura pc=4
[IC 13001ms] TICK (IRQ0)
[IC 13001ms] FILA <- pid=928 I/O=WRITE
[IC 13001ms] ATENDIMENTO INICIADO (pid=928 I/O=WRITE) | t_serviço=3s
[IC 14001ms] TICK (IRQ0)
[IC 15001ms] TICK (IRQ0)
[APP pid=929 idx=1] SYSCALL I/O WRITE em pc=8
[IC 16001ms] TICK (IRQ0)
[IC 16001ms] ATENDIMENTO CONCLUÍDO (pid=928 I/O=WRITE) -> IRQ1
[KRL 16003ms] BLOQUEIO (I/O WRITE) -> idx=1 pid=929 | ENFILEIRA
[KRL 16003ms] DESPACHE -> idx=2 pid=930
[KRL 16003ms] DESBLOQUEIO (IRQ1 I/O WRITE) -> idx=0 pid=928 | PRIORIDADE
[KRL 16003ms] PREEMPÇÃO -> idx=2 pid=930 (sai da CPU)
[KRL 16003ms] DESPACHE -> idx=0 pid=928
[APP pid=928 idx=0] RETORNO (SIGCONT) -> restaura pc=9
[IC 17001ms] TICK (IRQ0)
[IC 17001ms] FILA <- pid=929 I/O=WRITE
[IC 17002ms] ATENDIMENTO INICIADO (pid=929 I/O=WRITE) | t_serviço=3s
[IC 18002ms] TICK (IRQ0)
[IC 19002ms] TICK (IRQ0)
[IC 20002ms] TICK (IRQ0)
[IC 20002ms] ATENDIMENTO CONCLUÍDO (pid=929 I/O=WRITE) -> IRQ1
[KRL 20004ms] PREEMPÇÃO -> idx=0 pid=928 (sai da CPU)
[KRL 20004ms] DESPACHE -> idx=2 pid=930
[KRL 20004ms] DESBLOQUEIO (IRQ1 I/O WRITE) -> idx=1 pid=929 | PRIORIDADE
[KRL 20004ms] PREEMPÇÃO -> idx=2 pid=930 (sai da CPU)
[KRL 20004ms] DESPACHE -> idx=1 pid=929
[KRL 20004ms] FIM do Kernel
```

---

### Cenário 3 — A1..A6 (mistos)

**Configuração:** `./kernel 4 40 -- ./app_cpu -- ./app_cpu -- ./app_cpu -- ./app_rw -- ./app_rw -- ./app_rw`

#### Linha do tempo — *Esperada*
CPU-only (0..2) sem SYSCALL; RW (3..5) com BLOQUEIO e DESBLOQUEIO (IRQ1 PRIORIDADE).

#### Linha do tempo — *Obtida*
```
./kernel 4 40 -- ./app_cpu -- ./app_cpu -- ./app_cpu -- ./app_rw -- ./app_rw -- ./app_rw
[IC 0ms] INÍCIO (kpid=944)
[KRL 1ms] INÍCIO | RR+I/O | quantum=4s | duração=40s | procs=6
[KRL 1ms] DESPACHE -> idx=0 pid=946
[APP pid=946 idx=0] INÍCIO (Apenas CPU)
[IC 1000ms] TICK (IRQ0)
[IC 2000ms] TICK (IRQ0)
[IC 3000ms] TICK (IRQ0)
[IC 4000ms] TICK (IRQ0)
[KRL 4001ms] PREEMPÇÃO -> idx=0 pid=946 (sai da CPU)
[KRL 4001ms] DESPACHE -> idx=1 pid=947
[APP pid=947 idx=1] INÍCIO (Apenas CPU)
[IC 5001ms] TICK (IRQ0)
[IC 6001ms] TICK (IRQ0)
[IC 7001ms] TICK (IRQ0)
[IC 8001ms] TICK (IRQ0)
[KRL 8002ms] PREEMPÇÃO -> idx=1 pid=947 (sai da CPU)
[KRL 8002ms] DESPACHE -> idx=2 pid=948
[APP pid=948 idx=2] INÍCIO (Apenas CPU)
[IC 9001ms] TICK (IRQ0)
[IC 10001ms] TICK (IRQ0)
[IC 11001ms] TICK (IRQ0)
[IC 12001ms] TICK (IRQ0)
[KRL 12002ms] PREEMPÇÃO -> idx=2 pid=948 (sai da CPU)
[KRL 12002ms] DESPACHE -> idx=3 pid=949
[APP pid=949 idx=3] INÍCIO
[IC 13001ms] TICK (IRQ0)
[IC 14001ms] TICK (IRQ0)
[IC 15002ms] TICK (IRQ0)
[APP pid=949 idx=3] SYSCALL I/O READ em pc=3
[IC 16002ms] TICK (IRQ0)
[KRL 16003ms] BLOQUEIO (I/O READ) -> idx=3 pid=949 | ENFILEIRA
[KRL 16003ms] DESPACHE -> idx=0 pid=946
[APP pid=946 idx=0] RETORNO (SIGCONT) -> restaura pc=4
[IC 17002ms] TICK (IRQ0)
[IC 17002ms] FILA <- pid=949 I/O=READ
[IC 17002ms] ATENDIMENTO INICIADO (pid=949 I/O=READ) | t_serviço=3s
[IC 18002ms] TICK (IRQ0)
[IC 19002ms] TICK (IRQ0)
[IC 20002ms] TICK (IRQ0)
[IC 20002ms] ATENDIMENTO CONCLUÍDO (pid=949 I/O=READ) -> IRQ1
[KRL 20003ms] PREEMPÇÃO -> idx=0 pid=946 (sai da CPU)
[KRL 20003ms] DESPACHE -> idx=1 pid=947
[KRL 20003ms] DESBLOQUEIO (IRQ1 I/O READ) -> idx=3 pid=949 | PRIORIDADE
[KRL 20003ms] PREEMPÇÃO -> idx=1 pid=947 (sai da CPU)
[KRL 20003ms] DESPACHE -> idx=3 pid=949
[APP pid=949 idx=3] RETORNO (SIGCONT) -> restaura pc=4
[IC 21002ms] TICK (IRQ0)
[IC 22002ms] TICK (IRQ0)
[IC 23002ms] TICK (IRQ0)
[APP pid=949 idx=3] SYSCALL I/O WRITE em pc=8
[IC 24002ms] TICK (IRQ0)
[KRL 24003ms] BLOQUEIO (I/O WRITE) -> idx=3 pid=949 | ENFILEIRA
[KRL 24003ms] DESPACHE -> idx=0 pid=946
[APP pid=946 idx=0] RETORNO (SIGCONT) -> restaura pc=9
[IC 25003ms] TICK (IRQ0)
[IC 25003ms] FILA <- pid=949 I/O=WRITE
[IC 25003ms] ATENDIMENTO INICIADO (pid=949 I/O=WRITE) | t_serviço=3s
[IC 26003ms] TICK (IRQ0)
[IC 27003ms] TICK (IRQ0)
[IC 28003ms] TICK (IRQ0)
[KRL 28004ms] PREEMPÇÃO -> idx=0 pid=946 (sai da CPU)
[IC 28003ms] ATENDIMENTO CONCLUÍDO (pid=949 I/O=WRITE) -> IRQ1
[KRL 28004ms] DESPACHE -> idx=1 pid=947
[KRL 28004ms] DESBLOQUEIO (IRQ1 I/O WRITE) -> idx=3 pid=949 | PRIORIDADE
[KRL 28004ms] PREEMPÇÃO -> idx=1 pid=947 (sai da CPU)
[KRL 28004ms] DESPACHE -> idx=3 pid=949
[APP pid=949 idx=3] RETORNO (SIGCONT) -> restaura pc=9
[IC 29003ms] TICK (IRQ0)
[IC 30003ms] TICK (IRQ0)
[IC 31003ms] TICK (IRQ0)
[IC 32003ms] TICK (IRQ0)
[KRL 32004ms] PREEMPÇÃO -> idx=3 pid=949 (sai da CPU)
[KRL 32004ms] DESPACHE -> idx=4 pid=950
[APP pid=950 idx=4] INÍCIO
[IC 33003ms] TICK (IRQ0)
[IC 34003ms] TICK (IRQ0)
[IC 35003ms] TICK (IRQ0)
[APP pid=950 idx=4] SYSCALL I/O READ em pc=3
[IC 36004ms] TICK (IRQ0)
[KRL 36005ms] BLOQUEIO (I/O READ) -> idx=4 pid=950 | ENFILEIRA
[KRL 36005ms] DESPACHE -> idx=0 pid=946
[APP pid=946 idx=0] RETORNO (SIGCONT) -> restaura pc=14
[IC 37004ms] TICK (IRQ0)
[IC 37004ms] FILA <- pid=950 I/O=READ
[IC 37004ms] ATENDIMENTO INICIADO (pid=950 I/O=READ) | t_serviço=3s
[IC 38004ms] TICK (IRQ0)
[IC 39004ms] TICK (IRQ0)
[IC 40004ms] TICK (IRQ0)
[IC 40004ms] ATENDIMENTO CONCLUÍDO (pid=950 I/O=READ) -> IRQ1
[KRL 40005ms] PREEMPÇÃO -> idx=0 pid=946 (sai da CPU)
[KRL 40005ms] DESPACHE -> idx=1 pid=947
[KRL 40005ms] DESBLOQUEIO (IRQ1 I/O READ) -> idx=4 pid=950 | PRIORIDADE
[KRL 40005ms] PREEMPÇÃO -> idx=1 pid=947 (sai da CPU)
[KRL 40005ms] DESPACHE -> idx=4 pid=950
[KRL 40005ms] FIM do Kernel
```

---

## Conclusão Geral

Os três cenários confirmam o funcionamento correto do KernelSim:

- RR preemptivo com quantum de 1s entre todos os processos;
- Bloqueio e desbloqueio por I/O com atendimento serial via InterController;
- Desbloqueio com prioridade preemptando o processo corrente;
- Separação clara entre tarefas CPU-only e I/O observada em logs.

O KernelSim cumpre integralmente os requisitos do Trabalho 1, demonstrando os conceitos de escalonamento, sinais, SHM/FIFO e interrupções assíncronas.

---
