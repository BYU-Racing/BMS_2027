#include "include/ltc6811.h"
#include <string.h>

/* Datasheet timing (conservative edges of the spec) */
#define T_IDLE_SAFE_US   4000u      /* tIDLE min 4.3 ms: isoSPI may be idle after this */
#define T_SLEEP_SAFE_US  1500000u   /* tSLEEP min 1.8 s: core may be asleep after this */
#define T_WAKE_GAP_US    500u       /* tWAKE max 400 us, core asleep */
#define T_READY_GAP_US   20u        /* tREADY max 10 us, core in standby */
#define SPI_TIMEOUT_MS   5u

#define FRAME_MAX (4u + LTC6811_MAX_IC * (LTC6811_REG_BYTES + LTC6811_PEC_BYTES))

/* ---------- microsecond time base (DWT cycle counter) ---------- */
static inline uint32_t cyc_now(void) { return DWT->CYCCNT; }
static inline uint32_t cyc_to_us(uint32_t c) { return c / (SystemCoreClock / 1000000u); }

static void delay_us(uint32_t us)
{
    const uint32_t start = cyc_now();
    const uint32_t ticks = us * (SystemCoreClock / 1000000u);
    while ((uint32_t)(cyc_now() - start) < ticks) { }
}

/* ---------- PEC15: poly 0x4599, seed 0x0010, result << 1 ---------- */
uint16_t ltc6811_pec15(const uint8_t *data, size_t len)
{
    uint16_t rem = 16u;
    for (size_t i = 0; i < len; ++i) {
        for (int b = 7; b >= 0; --b) {
            const uint16_t in0 = (uint16_t)(((data[i] >> b) & 1u) ^ ((rem >> 14) & 1u));
            rem = (uint16_t)((rem << 1) & 0x7FFFu);
            if (in0) rem ^= 0x4599u;
        }
    }
    return (uint16_t)(rem << 1);
}

/* ---------- low level ---------- */
static inline void cs_low(const ltc6811_t *d)  { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET); }
static inline void cs_high(const ltc6811_t *d) { HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET); }

static void mark_comm(ltc6811_t *d) { d->last_comm_cyc = cyc_now(); d->comm_seen = 1u; }

/* One CSB pulse per IC so the wake propagates through the isoSPI chain. */
static void wake(ltc6811_t *d)
{
    uint32_t idle_us = 0xFFFFFFFFu;
    if (d->comm_seen) idle_us = cyc_to_us((uint32_t)(cyc_now() - d->last_comm_cyc));
    if (idle_us < T_IDLE_SAFE_US) return;                       /* still awake */

    const uint32_t gap = (idle_us >= T_SLEEP_SAFE_US) ? T_WAKE_GAP_US : T_READY_GAP_US;
    const uint8_t dummy = 0xFFu;
    for (uint8_t i = 0; i < d->n_ic; ++i) {
        cs_low(d);
        (void)HAL_SPI_Transmit(d->hspi, (uint8_t *)&dummy, 1, SPI_TIMEOUT_MS);
        cs_high(d);
        delay_us(gap);
    }
    mark_comm(d);
}

static void put_cmd(uint8_t *buf, uint16_t cmd)
{
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)cmd;
    const uint16_t pec = ltc6811_pec15(buf, 2);
    buf[2] = (uint8_t)(pec >> 8);
    buf[3] = (uint8_t)pec;
}

static ltc6811_status_t xfer(ltc6811_t *d, uint8_t *tx, uint8_t *rx, size_t len)
{
    HAL_StatusTypeDef st;
    cs_low(d);
    st = rx ? HAL_SPI_TransmitReceive(d->hspi, tx, rx, (uint16_t)len, SPI_TIMEOUT_MS)
            : HAL_SPI_Transmit(d->hspi, tx, (uint16_t)len, SPI_TIMEOUT_MS);
    cs_high(d);
    mark_comm(d);
    return (st == HAL_OK) ? LTC6811_OK : LTC6811_ERR_SPI;
}

/* ---------- public API ---------- */
ltc6811_status_t ltc6811_init(ltc6811_t *d, SPI_HandleTypeDef *hspi,
                              GPIO_TypeDef *cs_port, uint16_t cs_pin, uint8_t n_ic)
{
    if (!d || !hspi || n_ic == 0u || n_ic > LTC6811_MAX_IC) return LTC6811_ERR_ARG;
    memset(d, 0, sizeof(*d));
    d->hspi = hspi; d->cs_port = cs_port; d->cs_pin = cs_pin; d->n_ic = n_ic;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* enable DWT cycle counter */
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    cs_high(d);
    return LTC6811_OK;
}

ltc6811_status_t ltc6811_cmd(ltc6811_t *d, uint16_t cmd)
{
    uint8_t tx[4];
    wake(d);
    put_cmd(tx, cmd);
    return xfer(d, tx, NULL, sizeof tx);
}

ltc6811_status_t ltc6811_write_group(ltc6811_t *d, uint16_t cmd,
                                     const uint8_t data[][LTC6811_REG_BYTES])
{
    uint8_t tx[FRAME_MAX];
    size_t pos = 4;
    wake(d);
    put_cmd(tx, cmd);
    for (int ic = (int)d->n_ic - 1; ic >= 0; --ic) {      /* last IC in chain first */
        memcpy(&tx[pos], data[ic], LTC6811_REG_BYTES);
        const uint16_t pec = ltc6811_pec15(data[ic], LTC6811_REG_BYTES);
        tx[pos + 6] = (uint8_t)(pec >> 8);
        tx[pos + 7] = (uint8_t)pec;
        pos += LTC6811_REG_BYTES + LTC6811_PEC_BYTES;
    }
    return xfer(d, tx, NULL, pos);
}

ltc6811_status_t ltc6811_read_group(ltc6811_t *d, uint16_t cmd,
                                    uint8_t data[][LTC6811_REG_BYTES],
                                    uint16_t *pec_fail_mask)
{
    uint8_t tx[FRAME_MAX], rx[FRAME_MAX];
    const size_t per_ic = LTC6811_REG_BYTES + LTC6811_PEC_BYTES;
    const size_t len = 4u + d->n_ic * per_ic;
    uint16_t fail = 0;

    wake(d);
    memset(tx, 0xFF, len);
    put_cmd(tx, cmd);
    ltc6811_status_t s = xfer(d, tx, rx, len);
    if (s != LTC6811_OK) return s;

    for (uint8_t ic = 0; ic < d->n_ic; ++ic) {            /* IC 0 (nearest the MCU) comes first */
        const uint8_t *p = &rx[4u + ic * per_ic];
        const uint16_t rx_pec = (uint16_t)((p[6] << 8) | p[7]);
        memcpy(data[ic], p, LTC6811_REG_BYTES);
        if (ltc6811_pec15(p, LTC6811_REG_BYTES) != rx_pec) fail |= (uint16_t)(1u << ic);
    }
    if (pec_fail_mask) *pec_fail_mask = fail;
    return fail ? LTC6811_ERR_PEC : LTC6811_OK;
}

ltc6811_status_t ltc6811_read_cells(ltc6811_t *d,
                                    uint16_t cv[][LTC6811_CELLS_PER_IC],
                                    uint16_t *pec_fail_mask)
{
    static const uint16_t cmds[4] = { LTC6811_RDCVA, LTC6811_RDCVB, LTC6811_RDCVC, LTC6811_RDCVD };
    uint8_t grp[LTC6811_MAX_IC][LTC6811_REG_BYTES];
    uint16_t fail_all = 0;

    for (uint8_t ic = 0; ic < d->n_ic; ++ic)
        for (uint8_t c = 0; c < LTC6811_CELLS_PER_IC; ++c) cv[ic][c] = 0xFFFFu;

    for (uint8_t g = 0; g < 4u; ++g) {
        uint16_t fail = 0;
        ltc6811_status_t s = ltc6811_read_group(d, cmds[g], grp, &fail);
        if (s == LTC6811_ERR_SPI) return s;
        fail_all |= fail;
        for (uint8_t ic = 0; ic < d->n_ic; ++ic) {
            if (fail & (1u << ic)) continue;               /* keep 0xFFFF for this group */
            for (uint8_t k = 0; k < 3u; ++k)
                cv[ic][g * 3u + k] = (uint16_t)(grp[ic][2u * k] | (grp[ic][2u * k + 1u] << 8));
        }
    }
    if (pec_fail_mask) *pec_fail_mask = fail_all;
    return fail_all ? LTC6811_ERR_PEC : LTC6811_OK;
}

ltc6811_status_t ltc6811_selftest_cells(ltc6811_t *d)
{
    uint16_t cv[LTC6811_MAX_IC][LTC6811_CELLS_PER_IC];
    uint16_t fail = 0;

    ltc6811_status_t s = ltc6811_cmd(d, LTC6811_CVST(LTC6811_MD_7KHZ, 1u));
    if (s != LTC6811_OK) return s;
    HAL_Delay(4);                                          /* >2.3 ms @ 7 kHz; use vTaskDelay in the RTOS task */
    s = ltc6811_read_cells(d, cv, &fail);
    if (s != LTC6811_OK) return s;
    for (uint8_t ic = 0; ic < d->n_ic; ++ic)
        for (uint8_t c = 0; c < LTC6811_CELLS_PER_IC; ++c)
            if (cv[ic][c] != 0x9555u) return LTC6811_ERR_SELFTEST;
    return LTC6811_OK;
}

void ltc6811_pack_cfga(uint8_t cfg[LTC6811_REG_BYTES], uint8_t refon, uint8_t adcopt,
                       uint16_t vuv_mV, uint16_t vov_mV, uint16_t dcc_mask, uint8_t dcto)
{
    /* threshold = (VUV + 1) * 16 * 100 uV  ->  VUV = mV*10/16/... = mV/1.6 - 1 */
    uint16_t vuv = vuv_mV ? (uint16_t)(((uint32_t)vuv_mV * 10u) / 16u - 1u) & 0x0FFFu : 0u;
    uint16_t vov = vov_mV ? (uint16_t)(((uint32_t)vov_mV * 10u) / 16u)      & 0x0FFFu : 0u;
    cfg[0] = (uint8_t)(0xF8u | ((refon & 1u) << 2) | (adcopt & 1u)); /* GPIO pull-downs off */
    cfg[1] = (uint8_t)(vuv & 0xFFu);
    cfg[2] = (uint8_t)(((vov & 0x0Fu) << 4) | (vuv >> 8));
    cfg[3] = (uint8_t)(vov >> 4);
    cfg[4] = (uint8_t)(dcc_mask & 0xFFu);
    cfg[5] = (uint8_t)(((dcto & 0x0Fu) << 4) | ((dcc_mask >> 8) & 0x0Fu));
}
