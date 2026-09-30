#include "include/spi_utils.h"

// PEC15: init 0x0010, poly 0x4599, result shifted left by 1
static uint16_t pec15(const uint8_t *d, size_t len) {
  uint16_t rem = 16;
  for (size_t i = 0; i < len; i++) {
    for (int b = 7; b >= 0; b--) {
      uint16_t in = ((d[i] >> b) & 1) ^ ((rem >> 14) & 1);
      rem = (rem << 1) & 0x7FFF;
      if (in)
        rem ^= 0x4599;
    }
  }
  return rem << 1;
}

// Wake every device: one CS pulse per device, with a pause for each to power
// up
static void ltc_wakeup(void) {
  uint8_t dummy = 0xFF;
  for (int i = 0; i < N_DEVICES; i++) {
    LTC_CS_LOW();
    HAL_SPI_Transmit(&hspi2, &dummy, 1, 10);
    LTC_CS_HIGH();
    // t_wake is up to ~300 us per device; 1 ms is comfortably safe
    vTaskDelay(1);
    // osDelay(1); // use vTaskDelay(pdMS_TO_TICKS(1)) if in a task
  }
}

static void ltc_send_cmd(uint16_t cmd, uint8_t *buf4) {
  buf4[0] = cmd >> 8;
  buf4[1] = cmd & 0xFF;
  uint16_t p = pec15(buf4, 2);
  buf4[2] = p >> 8;
  buf4[3] = p & 0xFF;
}

// Returns 0 on success, negative on failure
int ltc_spi_selftest(void) {
  uint8_t cmd[4];
  // Test pattern: CFGR0 = 0xFC (REFON set), rest zero, different per-device
  // tag in CFGR5
  uint8_t wr[N_DEVICES][8];

  ltc_wakeup();

  // ---- WRCFGA: cmd, then 8 bytes per device (6 data + PEC), last device
  // first
  // ----
  ltc_send_cmd(CMD_WRCFGA, cmd);
  for (int dev = 0; dev < N_DEVICES; dev++) {
    memset(wr[dev], 0, 8);
    wr[dev][0] = 0xFC;
    wr[dev][5] = 0xA0 + dev;
    uint16_t p = pec15(wr[dev], 6);
    wr[dev][6] = p >> 8;
    wr[dev][7] = p & 0xFF;
  }
  LTC_CS_LOW();
  HAL_SPI_Transmit(&hspi2, cmd, 4, 10);
  for (int dev = N_DEVICES - 1; dev >= 0;
       dev--) // farthest device's data goes first
    HAL_SPI_Transmit(&hspi2, wr[dev], 8, 10);
  LTC_CS_HIGH();

  // ---- RDCFGA ----
  uint8_t rd[N_DEVICES][8];
  ltc_send_cmd(CMD_RDCFGA, cmd);
  LTC_CS_LOW();
  HAL_SPI_Transmit(&hspi2, cmd, 4, 10);
  for (int dev = 0; dev < N_DEVICES; dev++) // nearest device replies first
    HAL_SPI_Receive(&hspi2, rd[dev], 8, 10);
  LTC_CS_HIGH();

  // ---- Verify ----
  for (int dev = 0; dev < N_DEVICES; dev++) {
    uint16_t p = pec15(rd[dev], 6);
    if (rd[dev][6] != (p >> 8) || rd[dev][7] != (p & 0xFF))
      return -1 - dev; // PEC error
    if (memcmp(rd[dev], wr[dev], 6) != 0)
      return -100 - dev; // data mismatch
  }
  return 0;
}