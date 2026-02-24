/*
 * CELULA DE MANUFATURA - SIMPLIFICADO + LOGS SERIAL
 * Hardware: STM32F446RE (Nucleo)
 *
 */

#include "main.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <string.h>

// --- HANDLES GLOBAIS ---
UART_HandleTypeDef huart2;

// --- CONFIGURACOES ---
#define BUFFER_SIZE     2
#define TEMPO_PULSO     100   // ms
#define TEMPO_PICK      500   // ms
#define TEMPO_POLL      50    // ms

// --- CONFIGURACOES DE SIMULACAO ---
#define TEMPO_CICLO_M1      4000  // ms - tempo que M1 leva para processar
#define TEMPO_CICLO_M2      6000  // ms - tempo que M2 leva para processar
#define TEMPO_MOVER_ROBO    1500  // ms - deslocamento simulado do robo
#define TEMPO_RETIRADA_BUF  5000  // ms - operador retira peca do buffer

// --- ESTADO SIMULADO (substitui HAL_GPIO_ReadPin) ---
volatile uint8_t sim_m1_done   = 0;  // 1 = M1 terminou peca
volatile uint8_t sim_m2_done   = 0;  // 1 = M2 terminou peca
volatile uint8_t sim_rob_busy  = 0;  // 1 = Robo em movimento
volatile uint8_t sim_buff_full = 0;  // 1 = Buffer cheio (sem vaga fisica)

// Contadores internos de simulacao (usados pela TaskSensors)
volatile uint32_t timer_m1  = 0;
volatile uint32_t timer_m2  = 0;
volatile uint32_t timer_buf = 0;
volatile uint8_t  buf_pecas = 0;  // Quantas pecas estao no buffer fisico

// --- RTOS HANDLES ---
osThreadId_t taskControlHandle;
osThreadId_t taskSensorsHandle;
osSemaphoreId_t semRobot;
osSemaphoreId_t semBuffer;
osMessageQueueId_t qPedidos;
osMutexId_t mtxPrint;

volatile uint8_t sistema_em_emergencia = 0;

// --- PROTOTIPOS ---
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void TaskControl(void *argument);
void TaskSensors(void *argument);
void SimularMovimento(uint32_t ms);
void Log(const char* msg);

// ==============================================================================
// MAIN
// ==============================================================================
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART2_UART_Init();

    osKernelInitialize();

    semRobot  = osSemaphoreNew(1, 1, NULL);
    semBuffer = osSemaphoreNew(BUFFER_SIZE, BUFFER_SIZE, NULL);
    mtxPrint  = osMutexNew(NULL);
    qPedidos  = osMessageQueueNew(4, sizeof(uint8_t), NULL);

    const osThreadAttr_t control_attr = { .name = "Control", .stack_size = 512 * 4, .priority = osPriorityHigh };
    taskControlHandle = osThreadNew(TaskControl, NULL, &control_attr);

    const osThreadAttr_t sensors_attr = { .name = "Sensors", .stack_size = 512 * 4, .priority = osPriorityNormal };
    taskSensorsHandle = osThreadNew(TaskSensors, NULL, &sensors_attr);

    Log("--- SISTEMA INICIADO ---");
    Log("Aguardando maquinas...");

    // Inicia temporizadores das maquinas (substitui Pulso START_M1 / START_M2)
    timer_m1 = TEMPO_CICLO_M1 / TEMPO_POLL;
    timer_m2 = TEMPO_CICLO_M2 / TEMPO_POLL;

    osKernelStart();

    while (1) {}
}

// ==============================================================================
// TAREFA: CONTROLE DO ROBO
// Logica identica ao original - apenas Pulso() e ReadPin() substituidos
// ==============================================================================
void TaskControl(void *argument)
{
    uint8_t maquina_id;
    char buffer_msg[64];

    for(;;)
    {
        if (sistema_em_emergencia) {
            osDelay(100);
            continue;
        }

        // 1. Espera pedido de alguma maquina
        osStatus_t status = osMessageQueueGet(qPedidos, &maquina_id, NULL, osWaitForever);
        if (status != osOK) continue;

        sprintf(buffer_msg, "[CONTROL] Pedido recebido da Maquina %d", maquina_id);
        Log(buffer_msg);

        // 2. Garante vaga no buffer
        if (osSemaphoreAcquire(semBuffer, 10) != osOK) {
            Log("[CONTROL] Buffer cheio! Aguardando vaga...");
            osSemaphoreAcquire(semBuffer, osWaitForever);
        }
        Log("[CONTROL] Vaga no buffer reservada.");

        // 3. Pega o Robo (exclusao mutua)
        osSemaphoreAcquire(semRobot, osWaitForever);
        Log("[CONTROL] Robo alocado.");

        // --- SEQUENCIA DE MOVIMENTO ---

        // Vai ate a maquina (substitui Pulso GOTO_M1/M2 + while ROB_BUSY)
        sim_rob_busy = 1;
        Log("[ROBO] Indo buscar peca...");
        SimularMovimento(TEMPO_MOVER_ROBO);
        sim_rob_busy = 0;

        // Pega a peca (substitui Pulso PICK)
        osDelay(TEMPO_PICK);

        // Reinicia a maquina (substitui Pulso START_M1/M2)
        if (maquina_id == 1) {
            sim_m1_done = 0;
            timer_m1 = TEMPO_CICLO_M1 / TEMPO_POLL;
        } else {
            sim_m2_done = 0;
            timer_m2 = TEMPO_CICLO_M2 / TEMPO_POLL;
        }
        Log("[ROBO] Peca coletada. Maquina reiniciada.");

        // Vai pro Buffer (substitui Pulso GOTO_BUF + while ROB_BUSY)
        sim_rob_busy = 1;
        SimularMovimento(TEMPO_MOVER_ROBO);
        sim_rob_busy = 0;

        // Aguarda espaco fisico no buffer (substitui while ReadPin BUFF_FULL)
        while (sim_buff_full) osDelay(50);

        // Solta a peca (substitui Pulso DROP)
        osDelay(TEMPO_PICK);
        buf_pecas++;
        if (buf_pecas >= BUFFER_SIZE) sim_buff_full = 1;

        // Inicia temporizador de retirada do buffer pelo operador
        timer_buf = TEMPO_RETIRADA_BUF / TEMPO_POLL;

        Log("[ROBO] Peca entregue no buffer.");

        // Libera o robo
        osSemaphoreRelease(semRobot);
        Log("[CONTROL] Ciclo finalizado. Robo liberado.\r\n");
    }
}

// ==============================================================================
// TAREFA: SENSORES
// Logica identica ao original - substitui leituras GPIO por variaveis simuladas
// Tambem gerencia os temporizadores de simulacao das maquinas e do buffer
// ==============================================================================
void TaskSensors(void *argument)
{
    uint8_t last_m1   = 0;
    uint8_t last_m2   = 0;
    uint8_t last_buff = 0;  // Inicia em 0 (buffer vazio = sem falsa borda)

    for(;;)
    {
        // --- AVANCO DOS TEMPORIZADORES DE SIMULACAO ---

        if (timer_m1 > 0) {
            timer_m1--;
            if (timer_m1 == 0) sim_m1_done = 1;
        }

        if (timer_m2 > 0) {
            timer_m2--;
            if (timer_m2 == 0) sim_m2_done = 1;
        }

        // Operador retira peca do buffer apos tempo simulado
        if (timer_buf > 0) {
            timer_buf--;
            if (timer_buf == 0 && buf_pecas > 0) {
                buf_pecas--;
                if (buf_pecas < BUFFER_SIZE) sim_buff_full = 0;
            }
        }

        // --- LEITURA DO ESTADO SIMULADO ---
        uint8_t m1          = sim_m1_done;
        uint8_t m2          = sim_m2_done;
        uint8_t buff_sensor = sim_buff_full;

        if (!sistema_em_emergencia)
        {
            // Borda de subida M1
            if (m1 == 1 && last_m1 == 0) {
                uint8_t id = 1;
                osMessageQueuePut(qPedidos, &id, 0, 0);
                Log("[SENSOR] M1 Pronta.");
            }

            // Borda de subida M2
            if (m2 == 1 && last_m2 == 0) {
                uint8_t id = 2;
                osMessageQueuePut(qPedidos, &id, 0, 0);
                Log("[SENSOR] M2 Pronta.");
            }

            // Borda de descida do buffer (vaga liberada)
            if (buff_sensor == 0 && last_buff == 1) {
                osSemaphoreRelease(semBuffer);
                Log("[SENSOR] Buffer liberou uma vaga.");
            }
        }

        last_m1   = m1;
        last_m2   = m2;
        last_buff = buff_sensor;

        osDelay(TEMPO_POLL);
    }
}

// ==============================================================================
// HELPERS
// ==============================================================================

void SimularMovimento(uint32_t ms)
{
    osDelay(ms);
}

void Log(const char* msg)
{
    if (mtxPrint != NULL) osMutexAcquire(mtxPrint, osWaitForever);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 500);
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 500);
    if (mtxPrint != NULL) osMutexRelease(mtxPrint);
}

// ==============================================================================
// CONFIGURACOES DE HARDWARE
// ==============================================================================

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = 16;
    RCC_OscInitStruct.PLL.PLLN            = 336;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ            = 2;
    RCC_OscInitStruct.PLL.PLLR            = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { while(1); }
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { while(1); }
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) { while(1); }
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
}
