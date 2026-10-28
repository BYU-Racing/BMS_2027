#ifdef NUCLEO_DEBUG

#include "include/dbg_print.h"
#include "FreeRTOS.h"
#include "main.h"
#include "semphr.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>


#define DBG_BUF_SIZE 128

static UART_HandleTypeDef hdbg;
static SemaphoreHandle_t dbg_mutex;
static char dbg_buf[DBG_BUF_SIZE];

void dbg_init(void) {
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_LPUART1_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; /* PA2 TX, PA3 RX -> ST-LINK VCP */
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = GPIO_AF12_LPUART1;
  HAL_GPIO_Init(GPIOA, &gpio);

  hdbg.Instance = LPUART1;
  hdbg.Init.BaudRate = 115200;
  hdbg.Init.WordLength = UART_WORDLENGTH_8B;
  hdbg.Init.StopBits = UART_STOPBITS_1;
  hdbg.Init.Parity = UART_PARITY_NONE;
  hdbg.Init.Mode = UART_MODE_TX_RX;
  hdbg.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  hdbg.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  hdbg.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&hdbg);

  dbg_mutex = xSemaphoreCreateMutex();
}

void dbg_printf(const char *fmt, ...) {
  /* Before the scheduler starts, skip the mutex */
  int locked = 0;
  if (dbg_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    xSemaphoreTake(dbg_mutex, portMAX_DELAY);
    locked = 1;
  }

  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(dbg_buf, DBG_BUF_SIZE, fmt, args);
  va_end(args);

  if (len > 0) {
    if (len > DBG_BUF_SIZE - 1)
      len = DBG_BUF_SIZE - 1;
    HAL_UART_Transmit(&hdbg, (uint8_t *)dbg_buf, (uint16_t)len, 100);
  }

  if (locked)
    xSemaphoreGive(dbg_mutex);
}

#endif /* NUCLEO_DEBUG */