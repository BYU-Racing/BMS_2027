#ifndef LTC6811_H
#define LTC6811_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"   /* CubeMX: pulls in HAL + SPI_HandleTypeDef */
#include "include/constants.h"
// #include "task.h" /* allows the freeRTOS vTaskDelay functionality inside ltc6811.c file */

/* DEFINE PINS */
#define LTC_CS_PORT GPIOB

#ifndef LTC6811_MAX_IC
#define LTC6811_MAX_IC 12u
#endif

#define LTC6811_CELLS_PER_IC 12u
#define LTC6811_REG_BYTES    6u   /* data bytes per register group per IC */
#define LTC6811_PEC_BYTES    2u
#define LTC6811_CELL_LSB_UV  100u /* 100 uV per count, no offset */

/* ---- Command codes (datasheet Table 38, CC[10:0]) and on page 59 in the documentation-------------------- */

/* Write configuration Register Group A */
#define LTC6811_WRCFGA   0x0001u
/* Read configuration Group A */
#define LTC6811_RDCFGA   0x0002u
/* Read Cell Voltage Register Group A */
#define LTC6811_RDCVA    0x0004u
/* Read Cell Voltage Register Group B */
#define LTC6811_RDCVB    0x0006u
/* Read Cell Voltage Register Group C */
#define LTC6811_RDCVC    0x0008u
/* Read Cell Voltage Register Group D */
#define LTC6811_RDCVD    0x000Au
/* Read Auxiliary Register Group A */
#define LTC6811_RDAUXA   0x000Cu
/* Read Auxiliary Register Group B */
#define LTC6811_RDAUXB   0x000Eu
/* Read Status Register Group A */
#define LTC6811_RDSTATA  0x0010u
/* Read Status Register Group B */
#define LTC6811_RDSTATB  0x0012u
/* Write PWM Regiser Group */
#define LTC6811_WRPWM    0x0020u
/* Read PWM Register Group */
#define LTC6811_RDPWM    0x0022u
/* Clear Cell Voltage Register Groups */
#define LTC6811_CLRCELL  0x0711u
/* Clear Auxiliary Register Groups */
#define LTC6811_CLRAUX   0x0712u
/* Clear Status Register Groups */
#define LTC6811_CLRSTAT  0x0713u
/* Poll ADC Conversion Status */
#define LTC6811_PLADC    0x0714u

/* Parameterised commands */
#define LTC6811_ADCV(md, dcp, ch)  ((uint16_t)(0x0260u | ((md) << 7) | ((dcp) << 4) | (ch)))
#define LTC6811_ADAX(md, chg)      ((uint16_t)(0x0460u | ((md) << 7) | (chg)))
#define LTC6811_CVST(md, st)       ((uint16_t)(0x0207u | ((md) << 7) | ((st) << 5)))

/* MD[1:0] with ADCOPT=0 */
#define LTC6811_MD_422HZ  0u
#define LTC6811_MD_27KHZ  1u   /* fast,   all cells ~1.1 ms */
#define LTC6811_MD_7KHZ   2u   /* normal, all cells ~2.3 ms */
#define LTC6811_MD_26HZ   3u   /* filtered */

typedef enum {
    LTC6811_OK = 0,
    LTC6811_ERR_SPI,
    LTC6811_ERR_PEC,
    LTC6811_ERR_ARG,
    LTC6811_ERR_SELFTEST
} ltc6811_status_t;

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef      *cs_port;
    uint16_t           cs_pin;
    uint8_t            n_ic;                 /* ICs in the daisy chain */
    uint8_t            comm_seen;            /* 0 until first transaction */
    uint32_t           last_comm_cyc;        /* DWT->CYCCNT at last CSB rise */
} ltc6811_t;

ltc6811_status_t ltc6811_init(ltc6811_t *d, SPI_HandleTypeDef *hspi,
                              GPIO_TypeDef *cs_port, uint16_t cs_pin, uint8_t n_ic);

uint16_t ltc6811_pec15(const uint8_t *data, size_t len);

/* Broadcast command with no data (ADCV, ADAX, CLRCELL, ...) */
ltc6811_status_t ltc6811_cmd(ltc6811_t *d, uint16_t cmd);

/* data[i] = IC i, where IC 0 is the one wired to the STM32. Sent last-IC-first. */
ltc6811_status_t ltc6811_write_group(ltc6811_t *d, uint16_t cmd,
                                     const uint8_t data[][LTC6811_REG_BYTES]);

/* pec_fail_mask: bit i set => IC i failed PEC. Returns LTC6811_ERR_PEC if non-zero. */
ltc6811_status_t ltc6811_read_group(ltc6811_t *d, uint16_t cmd,
                                    uint8_t data[][LTC6811_REG_BYTES],
                                    uint16_t *pec_fail_mask);

/* cv[ic][cell], raw counts (x100 uV). Invalid ICs are set to 0xFFFF. */
ltc6811_status_t ltc6811_read_cells(ltc6811_t *d,
                                    uint16_t cv[][LTC6811_CELLS_PER_IC],
                                    uint16_t *pec_fail_mask);

/* CVST self test (7 kHz, self test 1). Every cell word should read 0x9555. */
ltc6811_status_t ltc6811_selftest_cells(ltc6811_t *d);

/* Build CFGR0..5. vuv/ovv in mV (0 disables the compare), dcc = discharge mask (bit0 = cell 1) */
void ltc6811_pack_cfga(uint8_t cfg[LTC6811_REG_BYTES], uint8_t refon, uint8_t adcopt,
                       uint16_t vuv_mV, uint16_t vov_mV, uint16_t dcc_mask, uint8_t dcto);

#endif