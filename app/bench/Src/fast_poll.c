/*
 * fast_poll.c -- how fast can we read every cell and thermistor? See
 * fast_poll.h.
 *
 * This is a BENCHMARK, not the final BMS loop: it busy-waits (burning CPU)
 * and keeps the chips awake permanently. Its job is to measure real
 * numbers so the final design can pick a sensible polling scheme.
 *
 * MEASURED on the bench (1 slave board, 16 MHz CPU, 500 kHz SPI):
 *   all cells + all 16 thermistors every scan, 7 kHz:   26.7 scans/s
 *   round-robin (cells + 1 mux channel per scan), 7 kHz: 144 scans/s,
 *       all 16 thermistors refreshed 18 times/s
 *   round-robin, 27 kHz fast mode:                       195 scans/s
 *
 * TAKEAWAYS for the real BMS:
 *   - Thermistors are the bottleneck (8 mux settings vs 1 cell conversion),
 *     and temperatures change slowly -- so read cells every scan and step
 *     through the mux channels round-robin (ADCVAX does cells + one channel
 *     in a single conversion).
 *   - Of a 5 ms fast-mode scan only ~1.6 ms is the chip converting; the
 *     rest is communication, mostly CPU time per byte at 16 MHz. Running
 *     the STM32 at 170 MHz (PLL), SPI at 1 MHz, and a table-driven PEC are
 *     the levers -- this matters most with 9 boards, where every frame is
 *     ~6x longer.
 */

#include <math.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "hw.h"
#include "isospi.h"
#include "led.h"
#include "thermistor.h"
#include "fast_poll.h"

/* ---- Knobs ------------------------------------------------------------- */

/* ADC mode: ISOSPI_MD_FAST (27 kHz, noisier), ISOSPI_MD_NORMAL (7 kHz),
 * or ISOSPI_MD_FILTERED (26 Hz). */
#define FAST_POLL_MD ISOSPI_MD_NORMAL

/* 0 = every scan reads all cells AND every thermistor (ADCV, then one ADAX
 *     per mux channel).
 * 1 = every scan reads all cells plus one mux channel (two thermistors) in
 *     a single ADCVAX conversion, stepping through the channels -- cells
 *     at the full scan rate, the whole thermistor set every 8 scans. */
#define FAST_POLL_ROUND_ROBIN 1

/* Wait after switching the mux before converting. The mux + op-amp buffer
 * + 100 ohm/10 nF filter settle in ~1 us; this is margin. */
#define MUX_SETTLE_US 100

/* Conversion waits: datasheet conversion time + margin. Every conversion
 * is preceded by a register clear (CLRCELL/CLRAUX), so a wait that's too
 * short shows up as "stale" in the report instead of silently returning
 * the previous result -- tune these by watching that counter. */
#if FAST_POLL_MD == ISOSPI_MD_FAST
#define ADCV_WAIT_US    1300
#define ADAX_WAIT_US    1300
#define ADCVAX_WAIT_US  1600
#define MODE_NAME       "27 kHz fast"
#elif FAST_POLL_MD == ISOSPI_MD_NORMAL
#define ADCV_WAIT_US    2600
#define ADAX_WAIT_US    2600
#define ADCVAX_WAIT_US  3400
#define MODE_NAME       "7 kHz normal"
#else
#define ADCV_WAIT_US    205000
#define ADAX_WAIT_US    205000
#define ADCVAX_WAIT_US  240000
#define MODE_NAME       "26 Hz filtered"
#endif

#define REPORT_PERIOD_US 1000000UL   /* print stats once a second */

/* ---- Constants ------------------------------------------------------------- */

#define CELLS_PER_DEVICE 12
#define NO_RESULT 0xFFFFU

/* CFGR0 base: REFON = 1 (reference stays up, so no power-up delay per
 * conversion), GPIO1/GPIO2 pull-downs off (analog inputs); select bits are
 * ORed in per channel. CFGR1-5 = 0: no OV/UV, no balancing. */
#define CFGR0_BASE ((uint8_t)((1U << 2) | (1U << 3) | (1U << 4)))

static const struct
{
    uint8_t mux;
    uint8_t channel;
    const char *label;
} therms[] = {
#define THERM_ENTRY(label, mux, channel) { (mux), (channel), (label) },
    HW_THERMISTORS(THERM_ENTRY)
#undef THERM_ENTRY
};
#define THERM_COUNT ((int)(sizeof(therms) / sizeof(therms[0])))

/* ---- Microsecond timing --------------------------------------------------
 * The FreeRTOS tick only resolves 1 ms, too coarse for 1-3 ms conversions.
 * The Cortex-M4's DWT cycle counter counts CPU clocks (16 per us here), so
 * it gives microsecond timing and accurate busy-waits. */
#define DEMCR         (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA  (1UL << 24)
#define DWT_CTRL      (*(volatile uint32_t *)0xE0001000UL)
#define DWT_CYCCNT    (*(volatile uint32_t *)0xE0001004UL)
#define CYCLES_PER_US (configCPU_CLOCK_HZ / 1000000UL)

static void timer_init(void)
{
    DEMCR |= DEMCR_TRCENA; /* turn on the debug/trace block the counter lives in */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1UL;       /* CYCCNTENA */
}

/* Unsigned subtraction handles the counter wrapping (every ~268 s). */
static uint32_t elapsed_us(uint32_t since)
{
    return (DWT_CYCCNT - since) / CYCLES_PER_US;
}

static void delay_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT;
    while (DWT_CYCCNT - start < us * CYCLES_PER_US) {}
}

/* Waits out a conversion. The isoSPI port drops to IDLE after ~5 ms with
 * no traffic, so a long (filtered-mode) wait needs a wake before the
 * results can be read; the fast modes never get there. */
static void conversion_wait(uint32_t us)
{
    delay_us(us);
    if (us > 4000U)
    {
        isospi_wakeup();
    }
}

/* ---- Helpers (see the tip in cell_test.c about sharing these) ------------ */

static uint16_t reg16(const uint8_t group[6], int index)
{
    return (uint16_t)(group[index * 2] | (group[index * 2 + 1] << 8));
}

static void print_line(const char *line)
{
    fputs(line, stdout);
}

/* Latest results and per-report-window error counters. */
static uint16_t cell_code[HW_ISOSPI_DEVICE_COUNT][CELLS_PER_DEVICE];
static uint16_t therm_code[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT];
static uint16_t vref_code[HW_ISOSPI_DEVICE_COUNT];
static unsigned pec_fails;
static unsigned stale_cells;
static unsigned stale_aux;

static int channel_used(int ch)
{
    for (int t = 0; t < THERM_COUNT; t++)
    {
        if (therms[t].channel == ch)
        {
            return 1;
        }
    }
    return 0;
}

/* Selects mux `ch`. No readback here (unlike the slow tests) -- it would
 * cost a frame per scan; the stale/PEC counters catch real trouble. */
static void select_channel(int ch)
{
    uint8_t cfg[HW_ISOSPI_DEVICE_COUNT][6] = { 0 };
    for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
    {
        cfg[dev][0] = (uint8_t)(CFGR0_BASE | HW_THERM_SELECT_BITS(ch));
    }
    isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);
}

static void read_cells(void)
{
    static const uint16_t rdcv[4] = {
        ISOSPI_CMD_RDCVA, ISOSPI_CMD_RDCVB, ISOSPI_CMD_RDCVC, ISOSPI_CMD_RDCVD,
    };
    uint8_t group[HW_ISOSPI_DEVICE_COUNT][6];
    int stale = 0;

    for (int g = 0; g < 4; g++)
    {
        pec_fails += (unsigned)isospi_read_group(rdcv[g], group, HW_ISOSPI_DEVICE_COUNT);
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            for (int c = 0; c < 3; c++)
            {
                cell_code[dev][g * 3 + c] = reg16(group[dev], c);
                stale |= cell_code[dev][g * 3 + c] == NO_RESULT;
            }
        }
    }
    stale_cells += (unsigned)stale;
}

/* Reads GPIO1/GPIO2 (AUXA) into every thermistor on mux channel `ch`. */
static void read_channel_therms(int ch)
{
    uint8_t aux_a[HW_ISOSPI_DEVICE_COUNT][6];
    int stale = 0;

    pec_fails += (unsigned)isospi_read_group(ISOSPI_CMD_RDAUXA, aux_a, HW_ISOSPI_DEVICE_COUNT);
    for (int t = 0; t < THERM_COUNT; t++)
    {
        if (therms[t].channel != ch)
        {
            continue;
        }
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            therm_code[dev][t] = reg16(aux_a[dev], therms[t].mux - 1);
            stale |= therm_code[dev][t] == NO_RESULT;
        }
    }
    stale_aux += (unsigned)stale;
}

/* VREF2 is the third value in AUXB. */
static void read_vref2(void)
{
    uint8_t aux_b[HW_ISOSPI_DEVICE_COUNT][6];
    pec_fails += (unsigned)isospi_read_group(ISOSPI_CMD_RDAUXB, aux_b, HW_ISOSPI_DEVICE_COUNT);
    for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
    {
        vref_code[dev] = reg16(aux_b[dev], 2);
    }
}

/* ---- Report ----------------------------------------------------------------- */

/* Appends "12.34" for a microsecond count, as milliseconds. */
static int append_ms(char *line, int len, size_t size, uint32_t us)
{
    if (len >= (int)size)
    {
        return len;
    }
    return len + snprintf(line + len, size - (size_t)len, "%lu.%02lu",
                          (unsigned long)(us / 1000U), (unsigned long)(us % 1000U / 10U));
}

static int append_report(char *line, size_t size, unsigned scans, uint32_t window_us,
                         uint32_t sum_us, uint32_t min_us, uint32_t max_us,
                         uint32_t cells_sum_us, uint32_t therms_sum_us)
{
    uint32_t avg_us = scans ? sum_us / scans : 0;
    /* Pure scan rate (scans back to back, no reporting), in 0.1 Hz. */
    uint32_t rate_dhz = avg_us ? 10000000UL / avg_us : 0;
    /* Measured rate over the window, reporting time included, in 0.1 Hz. */
    uint32_t real_dhz = window_us ? (uint32_t)((uint64_t)scans * 10000000ULL / window_us) : 0;

    int len = snprintf(line, size,
                       "=== Fast poll: %s, %s ===\n"
                       "  %lu.%lu scans/s back-to-back (%lu.%lu/s incl. reporting) | scan ",
                       MODE_NAME,
                       FAST_POLL_ROUND_ROBIN ? "all cells + 1 mux channel per scan (ADCVAX)"
                                             : "all cells + all thermistors every scan",
                       (unsigned long)(rate_dhz / 10U), (unsigned long)(rate_dhz % 10U),
                       (unsigned long)(real_dhz / 10U), (unsigned long)(real_dhz % 10U));
    len = append_ms(line, len, size, avg_us);
    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, " ms avg (");
    }
    len = append_ms(line, len, size, min_us);
    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, " min, ");
    }
    len = append_ms(line, len, size, max_us);
    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, " max)\n");
    }

#if FAST_POLL_ROUND_ROBIN
    if (len < (int)size)
    {
        /* Every used channel visited once = full thermistor refresh. */
        int used = 0;
        for (int ch = 0; ch < HW_THERM_MUX_CHANNELS; ch++)
        {
            used += channel_used(ch);
        }
        uint32_t set_dhz = used ? rate_dhz / (uint32_t)used : 0;
        len += snprintf(line + len, size - (size_t)len,
                        "  cells refresh %lu.%lu/s | full thermistor set refreshes %lu.%lu/s (every %d scans)\n",
                        (unsigned long)(rate_dhz / 10U), (unsigned long)(rate_dhz % 10U),
                        (unsigned long)(set_dhz / 10U), (unsigned long)(set_dhz % 10U), used);
    }
    (void)cells_sum_us;
    (void)therms_sum_us;
#else
    if (len < (int)size && scans)
    {
        len += snprintf(line + len, size - (size_t)len, "  per scan: cells ");
        len = append_ms(line, len, size, cells_sum_us / scans);
        if (len < (int)size)
        {
            len += snprintf(line + len, size - (size_t)len, " ms + thermistors ");
        }
        len = append_ms(line, len, size, therms_sum_us / scans);
        if (len < (int)size)
        {
            len += snprintf(line + len, size - (size_t)len, " ms\n");
        }
    }
#endif

    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len,
                        "  errors this window: PEC %u, stale cells %u, stale aux %u%s\n",
                        pec_fails, stale_cells, stale_aux,
                        (stale_cells || stale_aux) ? "  <-- raise the *_WAIT_US for this mode" : "");
    }

    for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT && len < (int)size; dev++)
    {
        int min_c = 0;
        int max_c = 0;
        uint32_t sum = 0;
        for (int c = 0; c < CELLS_PER_DEVICE; c++)
        {
            if (cell_code[dev][c] < cell_code[dev][min_c])
            {
                min_c = c;
            }
            if (cell_code[dev][c] > cell_code[dev][max_c])
            {
                max_c = c;
            }
            sum += cell_code[dev][c];
        }
        uint32_t avg = sum / CELLS_PER_DEVICE;
        len += snprintf(line + len, size - (size_t)len,
                        "  dev%d cells: min %u.%u mV (C%d) | max %u.%u mV (C%d) | avg %lu.%lu mV | stack %lu mV\n",
                        dev,
                        cell_code[dev][min_c] / 10U, cell_code[dev][min_c] % 10U, min_c,
                        cell_code[dev][max_c] / 10U, cell_code[dev][max_c] % 10U, max_c,
                        (unsigned long)(avg / 10U), (unsigned long)(avg % 10U),
                        (unsigned long)(sum / 10U));

        /* Thermistors, 8 to a line: "TH6 24.3", "--" missing, "SHORT",
         * "?" no data / off the curve. */
        for (int t = 0; t < THERM_COUNT && len < (int)size; t++)
        {
            if (t % 8 == 0)
            {
                len += snprintf(line + len, size - (size_t)len, "  dev%d temps:", dev);
            }

            float r = 0.0f;
            float temp_c = 0.0f;
            therm_status_t st = thermistor_from_divider(therm_code[dev][t], vref_code[dev], &r, &temp_c);
            if (len < (int)size)
            {
                if (st == THERM_OK)
                {
                    long t_dc = lroundf(temp_c * 10.0f);
                    long t_abs = t_dc < 0 ? -t_dc : t_dc;
                    len += snprintf(line + len, size - (size_t)len, "  %s %s%ld.%ld",
                                    therms[t].label, t_dc < 0 ? "-" : "", t_abs / 10, t_abs % 10);
                }
                else
                {
                    len += snprintf(line + len, size - (size_t)len, "  %s %s", therms[t].label,
                                    st == THERM_MISSING ? "--" : st == THERM_SHORT ? "SHORT" : "?");
                }
            }
            if ((t % 8 == 7 || t == THERM_COUNT - 1) && len < (int)size)
            {
                len += snprintf(line + len, size - (size_t)len, "\n");
            }
        }
    }

    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, "\n");
    }
    return len;
}

/* ---- Task -------------------------------------------------------------------- */

void fast_poll_task(void *argument)
{
    (void)argument;

    isospi_init();
    timer_init();

    vTaskDelay(pdMS_TO_TICKS(300)); /* let the flash tool's debug disconnect finish */

    /* Wake the chain, turn the reference on, and give the supplies and
     * VREF2 the same settle time the slow tests use. After this the loop
     * talks to the chips every few ms, so they never idle or sleep. */
    isospi_wakeup();
    select_channel(0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    isospi_wakeup();

    for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
    {
        vref_code[dev] = NO_RESULT;
        for (int t = 0; t < THERM_COUNT; t++)
        {
            therm_code[dev][t] = NO_RESULT;
        }
    }

    led_set(HW_LED_ACTIVE, 1);

    unsigned scans = 0;
    uint32_t sum_us = 0;
    uint32_t min_us = UINT32_MAX;
    uint32_t max_us = 0;
    uint32_t cells_sum_us = 0;
    uint32_t therms_sum_us = 0;
    uint32_t window_start = DWT_CYCCNT;
    int rr_channel = 0;

    for (;;)
    {
        uint32_t scan_start = DWT_CYCCNT;

#if FAST_POLL_ROUND_ROBIN
        /* Skip channels with nothing on them. */
        while (!channel_used(rr_channel))
        {
            rr_channel = (rr_channel + 1) % HW_THERM_MUX_CHANNELS;
        }

        /* One conversion: 12 cells + this channel's two thermistors. */
        select_channel(rr_channel);
        delay_us(MUX_SETTLE_US);
        isospi_send_cmd(ISOSPI_CMD_CLRCELL);
        isospi_send_cmd(ISOSPI_CMD_CLRAUX);
        isospi_send_cmd(ISOSPI_CMD_ADCVAX(FAST_POLL_MD));
        conversion_wait(ADCVAX_WAIT_US);
        read_cells();
        read_channel_therms(rr_channel);

        rr_channel = (rr_channel + 1) % HW_THERM_MUX_CHANNELS;
#else
        /* Cells. */
        isospi_send_cmd(ISOSPI_CMD_CLRCELL);
        isospi_send_cmd(ISOSPI_CMD_ADCV(FAST_POLL_MD));
        conversion_wait(ADCV_WAIT_US);
        read_cells();
        uint32_t cells_us = elapsed_us(scan_start);

        /* Thermistors: one ADAX per mux channel, both muxes at once. */
        int first = 1;
        for (int ch = 0; ch < HW_THERM_MUX_CHANNELS; ch++)
        {
            if (!channel_used(ch))
            {
                continue;
            }
            select_channel(ch);
            delay_us(MUX_SETTLE_US);
            isospi_send_cmd(ISOSPI_CMD_CLRAUX);
            isospi_send_cmd(ISOSPI_CMD_ADAX(FAST_POLL_MD));
            conversion_wait(ADAX_WAIT_US);
            read_channel_therms(ch);
            if (first)
            {
                read_vref2(); /* ADAX converts VREF2 too -- once a scan is plenty */
                first = 0;
            }
        }
        cells_sum_us += cells_us;
        therms_sum_us += elapsed_us(scan_start) - cells_us;
        (void)rr_channel;
#endif

        uint32_t scan_us = elapsed_us(scan_start);
        scans++;
        sum_us += scan_us;
        min_us = scan_us < min_us ? scan_us : min_us;
        max_us = scan_us > max_us ? scan_us : max_us;

        uint32_t window_us = elapsed_us(window_start);
        if (window_us >= REPORT_PERIOD_US)
        {
#if FAST_POLL_ROUND_ROBIN
            /* ADCVAX doesn't convert VREF2 -- measure it once per report
             * for the thermistor ratio (outside the timed scans). */
            isospi_send_cmd(ISOSPI_CMD_CLRAUX);
            isospi_send_cmd(ISOSPI_CMD_ADAX(FAST_POLL_MD));
            conversion_wait(ADAX_WAIT_US);
            read_vref2();
#endif
            led_set(HW_LED_FAULT, pec_fails != 0 || stale_cells != 0 || stale_aux != 0);

            static char line[HW_ISOSPI_DEVICE_COUNT * (256 + THERM_COUNT * 16) + 512];
            append_report(line, sizeof(line), scans, window_us, sum_us, min_us, max_us,
                          cells_sum_us, therms_sum_us);
            print_line(line);

            scans = 0;
            sum_us = 0;
            min_us = UINT32_MAX;
            max_us = 0;
            cells_sum_us = 0;
            therms_sum_us = 0;
            pec_fails = 0;
            stale_cells = 0;
            stale_aux = 0;

            /* Printing can take longer than the isoSPI idle timeout. */
            isospi_wakeup();
            window_start = DWT_CYCCNT;
        }
    }
}
