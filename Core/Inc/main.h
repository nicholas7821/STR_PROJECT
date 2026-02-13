/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PIN_EMG_STOP_Pin GPIO_PIN_13
#define PIN_EMG_STOP_GPIO_Port GPIOC
#define PIN_M1_DONE_Pin GPIO_PIN_0
#define PIN_M1_DONE_GPIO_Port GPIOC
#define PIN_M2_DONE_Pin GPIO_PIN_1
#define PIN_M2_DONE_GPIO_Port GPIOC
#define PIN_BUFF_FULL_Pin GPIO_PIN_2
#define PIN_BUFF_FULL_GPIO_Port GPIOC
#define PIN_ROB_BUSY_Pin GPIO_PIN_3
#define PIN_ROB_BUSY_GPIO_Port GPIOC
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define LD2_Pin GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define PIN_ROB_DROP_Pin GPIO_PIN_4
#define PIN_ROB_DROP_GPIO_Port GPIOC
#define PIN_START_M1_Pin GPIO_PIN_0
#define PIN_START_M1_GPIO_Port GPIOB
#define PIN_START_M2_Pin GPIO_PIN_1
#define PIN_START_M2_GPIO_Port GPIOB
#define PIN_ROB_GOTO_M1_Pin GPIO_PIN_10
#define PIN_ROB_GOTO_M1_GPIO_Port GPIOB
#define PIN_ROB_GOTO_M2_Pin GPIO_PIN_13
#define PIN_ROB_GOTO_M2_GPIO_Port GPIOB
#define PIN_ROB_GOTO_BUF_Pin GPIO_PIN_14
#define PIN_ROB_GOTO_BUF_GPIO_Port GPIOB
#define PIN_ROB_PICK_Pin GPIO_PIN_15
#define PIN_ROB_PICK_GPIO_Port GPIOB
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
