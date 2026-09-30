/*
 * ltc6811_ping.c - read-only liveness test for a daisy chain of LTC6811s.
 *
 * Wakes the chain, issues RDCFGA, and checks the PEC15 of each device's
 * reply. Read-only, so it will not disturb balancing / config state.
 *
 * NOT compiled or tested on hardware - verify timing constants against the
 * LTC6811 datasheet and adjust the TODOs for your board.
 */
#include "FreeRTOS.h"
#include "main.h"
#include "task.h"
#include <stdint.h>
#include <string.h>


extern SPI_HandleTypeDef hspi2;

/* ---- TODO: adjust for your board ------------------------------------- */
#define LTC_NUM_DEVICES 12u
#define LTC_CS_PORT GPIOB      /* TODO: your CS pin */
#define LTC_CS_PIN GPIO_PIN_12 /* TODO */
#define LTC_T_WAKE_US 300u     /* per-device wake time (max) */
#define LTC_SPI_TIMEOUT 5u     /* ms */

#define CMD_RDCFGA_HI 0x00
#define CMD_RDCFGA_LO 0x02

typedef struct {
  uint32_t attempts;
  uint32_t spi_errors;
  uint32_t pec_errors;       /* count of individual device PEC failures */
  uint32_t consecutive_fail; /* pings in a row where any device failed  */
  uint32_t last_ok_mask;     /* bit n = device n replied with good PEC  */
} ltc_ping_stats_t;

ltc_ping_stats_t g_ltc_ping; /* watch in debugger / expose over CAN */

/* ---- helpers ---------------------------------------------------------- */
static void dwt_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us) {
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000u);
  while ((DWT->CYCCNT - start) < ticks) {
  }
}

static inline void cs_low(void) {
  HAL_GPIO_WritePin(LTC_CS_PORT, LTC_CS_PIN, GPIO_PIN_RESET);
}
static inline void cs_high(void) {
  HAL_GPIO_WritePin(LTC_CS_PORT, LTC_CS_PIN, GPIO_PIN_SET);
}

/* PEC15: poly 0x4599, init 0x0010, result shifted left 1 (LSB = 0). */
static uint16_t pec15(const uint8_t *d, size_t len) {
  uint16_t rem = 16;
  while (len--) {
    rem ^= (uint16_t)(*d++) << 7;
    for (int i = 0; i < 8; i++) {
      rem = (rem & 0x4000) ? (uint16_t)((rem << 1) ^ 0x4599)
                           : (uint16_t)(rem << 1);
      rem &= 0x7FFF;
    }
  }
  return (uint16_t)(rem << 1);
}

/* One CS pulse per device so every part in the chain leaves SLEEP. */
static void wake_chain(void) {
  const uint8_t dummy = 0xFF;
  for (uint32_t i = 0; i < LTC_NUM_DEVICES; i++) {
    cs_low();
    HAL_SPI_Transmit(&hspi2, (uint8_t *)&dummy, 1, LTC_SPI_TIMEOUT);
    cs_high();
    delay_us(LTC_T_WAKE_US);
  }
}

/* ---- public API ------------------------------------------------------- */

/* Call once before using ltc6811_ping(). */
void ltc6811_ping_init(void) {
  dwt_init();
  cs_high();
  memset(&g_ltc_ping, 0, sizeof g_ltc_ping);
}

/*
 * Returns a bitmask of devices that answered RDCFGA with a valid PEC
 * (bit 0 = device closest to the master). A full chain returns
 * (1 << LTC_NUM_DEVICES) - 1.
 * Take your SPI mutex around this if other tasks share SPI2.
 */
uint32_t ltc6811_ping(void) {
  uint8_t cmd[4];
  uint8_t tx[LTC_NUM_DEVICES * 8];
  uint8_t rx[LTC_NUM_DEVICES * 8];
  uint32_t ok_mask = 0;

  g_ltc_ping.attempts++;

  wake_chain();

  cmd[0] = CMD_RDCFGA_HI;
  cmd[1] = CMD_RDCFGA_LO;
  uint16_t cmd_pec = pec15(cmd, 2);
  cmd[2] = (uint8_t)(cmd_pec >> 8);
  cmd[3] = (uint8_t)(cmd_pec & 0xFF);

  memset(tx, 0xFF, sizeof tx);
  memset(rx, 0x00, sizeof rx);

  cs_low();
  HAL_StatusTypeDef st =
      HAL_SPI_Transmit(&hspi2, cmd, sizeof cmd, LTC_SPI_TIMEOUT);
  if (st == HAL_OK) {
    st = HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof rx, LTC_SPI_TIMEOUT);
  }
  cs_high();

  if (st != HAL_OK) {
    g_ltc_ping.spi_errors++;
  } else {
    for (uint32_t dev = 0; dev < LTC_NUM_DEVICES; dev++) {
      const uint8_t *frame = &rx[dev * 8]; /* 6 data + 2 PEC */
      uint16_t rx_pec = ((uint16_t)frame[6] << 8) | frame[7];
      if (pec15(frame, 6) == rx_pec) {
        ok_mask |= (1u << dev);
      } else {
        g_ltc_ping.pec_errors++;
      }
    }
  }

  const uint32_t full = (1u << LTC_NUM_DEVICES) - 1u;
  g_ltc_ping.last_ok_mask = ok_mask;
  g_ltc_ping.consecutive_fail =
      (ok_mask == full) ? 0 : g_ltc_ping.consecutive_fail + 1;
  return ok_mask;
}

/*
 * Bring-up soak test: pings every period_ms forever. Run it from a
 * dedicated debug task (with normal polling disabled), or call
 * ltc6811_ping() directly from the DataAcquisition task.
 *
 * To confirm the ~2 s sleep behaviour, run it with a period > 2500 ms:
 * every ping should still succeed because wake_chain() runs each time.
 * Remove wake_chain() from the ping temporarily and the same test should
 * start failing once the chain has gone to sleep.
 */
void ltc6811_ping_loop(uint32_t period_ms) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    (void)ltc6811_ping();
    vTaskDelayUntil(&last, pdMS_TO_TICKS(period_ms));
  }
}