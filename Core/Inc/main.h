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
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdbool.h>

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
#define DIG_OUT_0_Pin GPIO_PIN_0
#define DIG_OUT_0_GPIO_Port GPIOA
#define DIG_OUT_1_Pin GPIO_PIN_1
#define DIG_OUT_1_GPIO_Port GPIOA
#define DIG_OUT_2_Pin GPIO_PIN_2
#define DIG_OUT_2_GPIO_Port GPIOA
#define DIG_OUT_3_Pin GPIO_PIN_4
#define DIG_OUT_3_GPIO_Port GPIOA
#define DIG_OUT_4_Pin GPIO_PIN_5
#define DIG_OUT_4_GPIO_Port GPIOA
#define DIG_OUT_5_Pin GPIO_PIN_10
#define DIG_OUT_5_GPIO_Port GPIOA
#define DIG_OUT_6_Pin GPIO_PIN_15
#define DIG_OUT_6_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
