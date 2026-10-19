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
#include "stm32f1xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ENC_RA_Pin GPIO_PIN_0
#define ENC_RA_GPIO_Port GPIOA
#define ENC_RB_Pin GPIO_PIN_1
#define ENC_RB_GPIO_Port GPIOA
#define BT_TX_Pin GPIO_PIN_10
#define BT_TX_GPIO_Port GPIOB
#define BT_RX_Pin GPIO_PIN_11
#define BT_RX_GPIO_Port GPIOB
#define BIN1_Pin GPIO_PIN_12
#define BIN1_GPIO_Port GPIOB
#define BIN2_Pin GPIO_PIN_13
#define BIN2_GPIO_Port GPIOB
#define AIN1_Pin GPIO_PIN_14
#define AIN1_GPIO_Port GPIOB
#define AIN2_Pin GPIO_PIN_15
#define AIN2_GPIO_Port GPIOB
#define PWMA_Pin GPIO_PIN_8
#define PWMA_GPIO_Port GPIOA
#define PWMB_Pin GPIO_PIN_11
#define PWMB_GPIO_Port GPIOA
#define ENC_LA_Pin GPIO_PIN_6
#define ENC_LA_GPIO_Port GPIOB
#define ENC_LB_Pin GPIO_PIN_7
#define ENC_LB_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
