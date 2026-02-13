# Documentação Técnica: Controle de Célula de Manufatura com STM32 e FreeRTOS

Este documento detalha a implementação do firmware desenvolvido no **STM32CubeIDE** para o controle de uma célula de manufatura automatizada. O projeto utiliza o sistema operacional de tempo real **FreeRTOS (CMSIS-V2)** para gerenciar a concorrência, sincronização e comunicação entre os componentes do sistema.

---

## 1. Visão Geral do Sistema
O objetivo do firmware é coordenar três agentes principais:
1.  **Duas Máquinas de Processamento (M1 e M2):** Geram peças prontas de forma assíncrona.
2.  **Robô Industrial:** Recurso compartilhado que transporta peças das máquinas para a esteira.
3.  **Buffer de Saída (Esteira):** Armazenamento limitado (capacidade = 2 peças).

O controle deve garantir que o robô atenda as máquinas por ordem de chegada, nunca pegue uma peça se o buffer estiver cheio (prevenção de deadlock) e respeite a exclusividade de uso do robô.

---

## 2. Configuração de Hardware (Pinout)

A escolha dos pinos foi baseada na disponibilidade da placa de desenvolvimento **STM32 Nucleo-F446RE**, utilizando portas GPIO padrão.

### 2.1 Entradas (Sensores)
Os sensores são configurados como entradas digitais. No código, assume-se lógica positiva (HIGH = Ativo), exceto para o botão de emergência.

| Pino | Label | Função | Configuração | Motivo da Escolha |
| :--- | :--- | :--- | :--- | :--- |
| **PC0** | `PIN_M1_DONE` | Sinal de "Peça Pronta" na Máquina 1. | Input, Pull-Down | Garante nível lógico 0 quando o sensor está desconectado ou inativo. |
| **PC1** | `PIN_M2_DONE` | Sinal de "Peça Pronta" na Máquina 2. | Input, Pull-Down | Mesmo motivo de M1. |
| **PC2** | `PIN_BUFF_FULL` | Sensor de Buffer Cheio/Ocupado. | Input, Pull-Down | Detecta presença de peças na saída. |
| **PC3** | `PIN_ROB_BUSY` | Feedback de movimento do robô. | Input, Pull-Down | Permite que o software saiba quando o robô terminou um movimento físico. |
| **PC13** | `PIN_EMG` | Botão de Emergência. | Input, Pull-Up | Botão de usuário da placa Nucleo (Azul). Geralmente é Active Low. |

### 2.2 Saídas (Atuadores)
Sinais de comando enviados para os controladores das máquinas e do robô.

| Pino | Label | Função |
| :--- | :--- | :--- |
| **PB0** | `PIN_START_M1` | Comando de pulso para iniciar ciclo da Máquina 1. |
| **PB1** | `PIN_START_M2` | Comando de pulso para iniciar ciclo da Máquina 2. |
| **PB10** | `PIN_GOTO_M1` | Comanda robô ir para posição M1. |
| **PB13** | `PIN_GOTO_M2` | Comanda robô ir para posição M2. |
| **PB14** | `PIN_GOTO_BUF` | Comanda robô ir para posição do Buffer. |
| **PB15** | `PIN_PICK` | Comanda garra do robô (Pegar). |
| **PC4** | `PIN_DROP` | Comanda garra do robô (Soltar). |

---

## 3. Arquitetura de Software (FreeRTOS)

O sistema foi dividido em duas tarefas (Tasks) principais para desacoplar a leitura rápida dos sensores da lógica lenta de movimentação do robô.

### 3.1 Tarefas (Tasks)

#### **A. TaskSensors (Prioridade: Normal)**
*   **Responsabilidade:** Monitorar continuamente os sensores de entrada (polling a cada 50ms).
*   **Lógica:**
    1.  Lê o estado atual dos pinos.
    2.  Detecta **bordas de subida** (mudança de 0 para 1) nos sensores `M1_DONE` e `M2_DONE`.
    3.  Ao detectar que uma máquina terminou, envia um "pedido" (ID da máquina) para a **Fila de Pedidos**.
    4.  Detecta **borda de descida** (mudança de 1 para 0) no sensor `BUFF_FULL`. Isso indica que uma peça foi removida do buffer, liberando uma vaga.
    5.  Ao detectar vaga livre, libera o **Semáforo do Buffer** (`osSemaphoreRelease`).
    6.  Monitora o botão de Emergência e seta a flag global de segurança se pressionado.

#### **B. TaskControl (Prioridade: Alta)**
*   **Responsabilidade:** Gerenciar a sequência lógica de transporte do robô.
*   **Lógica (Passo a Passo):**
    1.  **Espera Pedido:** Bloqueia aguardando mensagem na fila (`osMessageQueueGet`). O robô fica parado aqui se não houver peças prontas.
    2.  **Reserva Buffer:** Tenta adquirir o semáforo do buffer (`osSemaphoreAcquire`). Se o contador estiver em 0 (buffer cheio), a tarefa bloqueia aqui. **Isso previne o Deadlock:** o robô não sai do lugar para pegar uma peça se não tiver certeza que poderá entregá-la.
    3.  **Reserva Robô:** Adquire o Mutex do Robô (`osSemaphoreAcquire`). Garante acesso exclusivo.
    4.  **Execução:**
        *   Manda robô ir até a máquina solicitada.
        *   Pega a peça (`PICK`).
        *   Reinicia a máquina (`START`).
        *   Leva ao buffer (`GOTO_BUF`).
        *   Espera sensor físico do buffer liberar (segurança redundante).
        *   Solta a peça (`DROP`).
    5.  **Liberação:** Devolve o Mutex do Robô.

---

## 4. Objetos de Sincronização (IPC)

A escolha correta dos objetos de comunicação entre processos (IPC) é o coração deste projeto.

### 4.1 Fila de Mensagens (`qPedidos`)
*   **Tipo:** `osMessageQueueId_t`
*   **Capacidade:** 4 elementos de `uint8_t`.
*   **Por que usar Fila?**
    *   As máquinas podem terminar quase ao mesmo tempo. Se usássemos apenas uma variável global, um evento poderia sobrescrever o outro.
    *   A fila funciona como um buffer FIFO (First-In, First-Out), garantindo que os pedidos sejam atendidos na ordem exata em que ocorreram.
    *   Permite comunicação assíncrona entre a `TaskSensors` (produtora) e `TaskControl` (consumidora).

### 4.2 Semáforo Contador (`semBuffer`)
*   **Tipo:** `osSemaphoreId_t` (Counting Semaphore)
*   **Contagem Inicial:** 2 (Máxima = 2).
*   **Por que usar Semáforo Contador?**
    *   O buffer tem capacidade física limitada (2 slots).
    *   O semáforo modela perfeitamente esses "recursos disponíveis".
    *   Cada vez que o robô vai pegar uma peça, ele decrementa o semáforo (`Acquire`). Se chegar a 0, ele sabe que não tem vaga.
    *   Cada vez que uma peça sai da esteira (sensor), o semáforo é incrementado (`Release`).

### 4.3 Semáforo Binário / Mutex (`semRobot`)
*   **Tipo:** `osSemaphoreId_t` (Binary Semaphore)
*   **Contagem Inicial:** 1.
*   **Por que usar Mutex?**
    *   O robô é um **Recurso Crítico Compartilhado**. Ele não pode receber comando de ir para M1 enquanto está indo para M2.
    *   O semáforo garante exclusão mútua: apenas quem tem a "chave" (token) pode comandar o robô.

---

## 5. Tratamento de Emergência
O sistema possui uma variável global volátil `sistema_em_emergencia`.
*   A `TaskSensors` detecta o botão pressionado imediatamente.
*   Ela seta a variável para `1` e força todas as saídas críticas (`PICK`, `DROP`, motores) para nível baixo (DESLIGADO) via hardware (`HAL_GPIO_WritePin`).
*   A `TaskControl` verifica essa variável no início de seu loop. Se estiver ativa, ela suspende a operação e não processa novos pedidos até que a emergência seja limpa.

---

## 6. Fluxograma de Decisão (Resumo)

1.  **Sensor:** M1 ficou pronta? -> **Enfileira Pedido (ID=1)**.
2.  **Controle:** Tem pedido na fila? -> Sim, peguei ID=1.
3.  **Controle:** Tem vaga no buffer? (Semáforo > 0?) -> Sim, decrementa semáforo.
4.  **Controle:** O Robô está livre? -> Sim, bloqueia robô.
5.  **Ação:** Robô executa transporte M1 -> Buffer.
6.  **Fim:** Libera Robô.
7.  **Sensor:** Peça saiu do buffer? -> **Incrementa Semáforo**.
