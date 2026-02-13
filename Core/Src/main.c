/*
 * Hardware: STM32F446RE (Nucleo)
 */

#include "main.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

// --- PINOUT ---
// Entradas
#define PIN_M1_DONE     GPIO_PIN_0 // PC0
#define PIN_M2_DONE     GPIO_PIN_1 // PC1
#define PIN_BUFF_FULL   GPIO_PIN_2 // PC2
#define PIN_ROB_BUSY    GPIO_PIN_3 // PC3
#define PIN_EMG         GPIO_PIN_13 // PC13 (Botao Azul)

// Saidas
#define PIN_START_M1    GPIO_PIN_0  // PB0
#define PIN_START_M2    GPIO_PIN_1  // PB1
#define PIN_GOTO_M1     GPIO_PIN_10 // PB10
#define PIN_GOTO_M2     GPIO_PIN_13 // PB13
#define PIN_GOTO_BUF    GPIO_PIN_14 // PB14
#define PIN_PICK        GPIO_PIN_15 // PB15
#define PIN_DROP        GPIO_PIN_4  // PC4

// --- CONFIGURACOES ---
#define BUFFER_SIZE     2
#define TEMPO_PULSO     100  // ms
#define TEMPO_PICK      500  // ms
#define TEMPO_POLL      50   // ms

// --- RTOS HANDLES ---
osThreadId_t taskControlHandle;
osThreadId_t taskSensorsHandle;
osSemaphoreId_t semRobot;    // Mutex do robo
osSemaphoreId_t semBuffer;   // Contador de espaco no buffer
osMessageQueueId_t qPedidos; // Fila de pedidos das maquinas

// Variavel global de emergencia
volatile uint8_t sistema_em_emergencia = 0;

// --- PROTOTIPOS ---
void TaskControl(void *argument);
void TaskSensors(void *argument);
void Pulso(GPIO_TypeDef* port, uint16_t pin, uint32_t ms);

// Inicializacao chamada no main.c
void Manufacturing_Init(void) {
    // Cria semaforos
    semRobot = osSemaphoreNew(1, 1, NULL); // Binario, inicia livre
    semBuffer = osSemaphoreNew(BUFFER_SIZE, BUFFER_SIZE, NULL); // Contador, inicia cheio (2 vagas)

    // Cria fila (ate 4 pedidos pendentes)
    qPedidos = osMessageQueueNew(4, sizeof(uint8_t), NULL);

    // Cria as tarefas
    const osThreadAttr_t control_attr = { .name = "Control", .stack_size = 512 * 4, .priority = osPriorityHigh };
    taskControlHandle = osThreadNew(TaskControl, NULL, &control_attr);

    const osThreadAttr_t sensors_attr = { .name = "Sensors", .stack_size = 256 * 4, .priority = osPriorityNormal };
    taskSensorsHandle = osThreadNew(TaskSensors, NULL, &sensors_attr);

    // Partida inicial nas maquinas
    Pulso(GPIOB, PIN_START_M1, TEMPO_PULSO);
    Pulso(GPIOB, PIN_START_M2, TEMPO_PULSO);
}

// Tarefa principal: Controla o Robo
void TaskControl(void *argument) {
    uint8_t maquina_id;

    for(;;) {
        // 0. Se emergencia, nao faz nada
        if (sistema_em_emergencia) {
            osDelay(100);
            continue;
        }

        // 1. Espera pedido de alguma maquina (Bloqueia aqui se nao tiver nada)
        osStatus_t status = osMessageQueueGet(qPedidos, &maquina_id, NULL, osWaitForever);
        if (status != osOK) continue;

        // 2. Garante vaga no buffer (Evita Deadlock: so pega peca se tiver onde por)
        osSemaphoreAcquire(semBuffer, osWaitForever);

        // 3. Pega o Robo (Exclusao mutua)
        osSemaphoreAcquire(semRobot, osWaitForever);

        // --- SEQUENCIA DE MOVIMENTO ---

        // Vai ate a maquina
        uint16_t pino_goto = (maquina_id == 1) ? PIN_GOTO_M1 : PIN_GOTO_M2;
        Pulso(GPIOB, pino_goto, TEMPO_PULSO);

        // Espera robo chegar (Busy = 0)
        while(HAL_GPIO_ReadPin(GPIOC, PIN_ROB_BUSY)) osDelay(50);

        // Pega a peca
        Pulso(GPIOB, PIN_PICK, TEMPO_PICK);

        // Manda maquina trabalhar de novo (libera ela)
        uint16_t pino_start = (maquina_id == 1) ? PIN_START_M1 : PIN_START_M2;
        Pulso(GPIOB, pino_start, TEMPO_PULSO);

        // Vai pro Buffer
        Pulso(GPIOB, PIN_GOTO_BUF, TEMPO_PULSO);
        while(HAL_GPIO_ReadPin(GPIOC, PIN_ROB_BUSY)) osDelay(50);

        // Espera buffer estar livre fisicamente (sensor)
        while(HAL_GPIO_ReadPin(GPIOC, PIN_BUFF_FULL) == GPIO_PIN_SET) osDelay(50);

        // Solta a peca
        Pulso(GPIOC, PIN_DROP, TEMPO_PICK);

        // --- FIM DA SEQUENCIA ---

        // Libera o robo
        osSemaphoreRelease(semRobot);
    }
}

// Tarefa de monitoramento dos sensores
void TaskSensors(void *argument) {
    uint8_t last_m1 = 0, last_m2 = 0, last_buff = 1; // 1 = buffer cheio/ocupado? Ajustar conforme logica

    for(;;) {
        // Leitura dos pinos
        uint8_t m1 = HAL_GPIO_ReadPin(GPIOC, PIN_M1_DONE);
        uint8_t m2 = HAL_GPIO_ReadPin(GPIOC, PIN_M2_DONE);
        uint8_t buff_sensor = HAL_GPIO_ReadPin(GPIOC, PIN_BUFF_FULL); // 0 = Livre, 1 = Ocupado
        uint8_t emg = HAL_GPIO_ReadPin(GPIOC, PIN_EMG); // 0 = Pressionado (Active Low)

        // Logica de Emergencia
        if (emg == 0) {
            sistema_em_emergencia = 1;
            // Desliga saidas criticas
            HAL_GPIO_WritePin(GPIOB, 0xFFFF, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, PIN_DROP, GPIO_PIN_RESET);
        } else {
            sistema_em_emergencia = 0;
        }

        if (!sistema_em_emergencia) {
            // Detecta borda de subida M1 (Ficou pronta)
            if (m1 == 1 && last_m1 == 0) {
                uint8_t id = 1;
                osMessageQueuePut(qPedidos, &id, 0, 0);
            }

            // Detecta borda de subida M2
            if (m2 == 1 && last_m2 == 0) {
                uint8_t id = 2;
                osMessageQueuePut(qPedidos, &id, 0, 0);
            }

            // Detecta que liberou espaco no buffer (Borda de descida do sensor Full)
            // Assumindo: 1 = Cheio, 0 = Vazio/Espaco
            if (buff_sensor == 0 && last_buff == 1) {
                osSemaphoreRelease(semBuffer); // Incrementa contador de vagas
            }
        }

        last_m1 = m1;
        last_m2 = m2;
        last_buff = buff_sensor;

        osDelay(TEMPO_POLL);
    }
}

// Helper simples pra dar pulso num pino
void Pulso(GPIO_TypeDef* port, uint16_t pin, uint32_t ms) {
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    osDelay(ms);
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}
