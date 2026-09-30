#pragma once
#include "main.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

extern SPI_HandleTypeDef hspi2;

#define LTC_CS_LOW()                                                           \
  HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_RESET)
#define LTC_CS_HIGH()                                                          \
  HAL_GPIO_WritePin(LTC_CS_GPIO_Port, LTC_CS_Pin, GPIO_PIN_SET)

#define N_DEVICES 1 // number of LTC6811s in the chain
#define CMD_WRCFGA 0x0001
#define CMD_RDCFGA 0x0002

static uint16_t pec15(const uint8_t *d, size_t len);

static void ltc_wakeup(void);

static void ltc_send_cmd(uint16_t cmd, uint8_t *buf4);

int ltc_spi_selftest(void);