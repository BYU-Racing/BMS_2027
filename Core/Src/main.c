/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"

static void delay_ms(uint32_t milliseconds)
{
    /* Discard a partial interval, then count hardware timer wraps.
     * No SysTick interrupt, HAL tick or RTOS scheduler is required. */
    (void)SysTick->CTRL;
    for (uint32_t elapsed = 0; elapsed < milliseconds; elapsed++)
    {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0U) {}
    }
}

int main(void)
{
    /* Use the application vectors even when launched by a bootloader. */
    SCB->VTOR = FLASH_BASE;
    __DSB();
    __ISB();
    SystemCoreClockUpdate();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
    GPIO_InitTypeDef led = {0};
    led.Pin = GPIO_PIN_0;
    led.Mode = GPIO_MODE_OUTPUT_PP;
    led.Pull = GPIO_NOPULL;
    led.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &led);

    /* Poll SysTick at 1 ms using HCLK; deliberately leave TICKINT disabled. */
    SysTick->CTRL = 0U;
    SysTick->LOAD = SystemCoreClock / 1000U - 1U;
    SysTick->VAL = 0U;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;

    for (;;)
    {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0);
        delay_ms(500);
    }
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {}
}
