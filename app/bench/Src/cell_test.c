/*
 * cell_test.c -- the DETAILED debug report: everything the LTC6811 can
 * tell us, with raw values, so problems can be traced. See cell_test.h.
 *
 * Use this when something looks wrong in the compact board check: it
 * shows raw millivolts, computed thermistor resistance, VREF2, the chip's
 * own sum-of-cells and supplies, whether each chip actually slept, and
 * whether the mux really switched. It also has a "hold" mode for probing
 * with a multimeter (THERM_HOLD_CHANNEL).
 *
 * Each reading: wake -> settle 1 s -> cells + status -> each mux channel
 * -> print -> 6 s of silence so the chips go to sleep.
 */

#include <math.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "hw.h"
#include "isospi.h"
#include "led.h"
#include "thermistor.h"
#include "cell_test.h"

/* ---- Knobs ------------------------------------------------------------- */

/* 1 = measure and print the 12 cells + status (stack, die temp, VA/VD)
 * each reading; 0 = thermistors only (handy when debugging just those). */
#define CELL_TEST_MEASURE_CELLS 1

/* 1 = 26 Hz filtered mode (quietest, ~200 ms per conversion),
 * 0 = 7 kHz normal mode (~2 ms per conversion, noisier). */
#define CELL_TEST_FILTERED 1

/* Bench debug: set to a mux channel (0-7) to park both muxes on it with
 * the chip kept awake and VREF2 on continuously, printing the raw voltages
 * about twice a second -- so the divider can be probed with a multimeter
 * (VREF2 only exists while the chip is awake). -1 = normal cycle. */
#define THERM_HOLD_CHANNEL -1

/* RUGBY INPUT: after a wake from sleep, wait this long before sampling.
 * VREG and VREF2 (which feeds every thermistor divider) need time to reach
 * steady state; sampling sooner reads dividers whose supply is still
 * ramping. Must stay under the chip's ~2 s watchdog. */
#define THERM_POWER_SETTLE_MS 1000

/* RUGBY INPUT: sample-then-sleep. Silence between readings, well past the
 * ~2 s watchdog, so every chip drops to SLEEP (config reset, VREG off) and
 * each reading exercises a full wake-up. */
#define SLEEP_WAIT_MS 6000

/* Above this a reading is flagged as a possible short (or a cell far too
 * hot) -- 80 C is ~1.67 kohm on the thermistor curve. */
#define THERM_SHORT_ABOVE_C 80

/* ---- Timing for the chosen mode ---------------------------------------- */

#if CELL_TEST_FILTERED
#define ADC_MD          ISOSPI_MD_FILTERED
#define ADCV_WAIT_MS    210   /* ~201 ms conversion + margin */
#define ADSTAT_WAIT_MS  140
#define ADAX_WAIT_MS    420   /* generous: never seen stale, never measured exactly */
#else
/* Normal mode: the ~2.3 ms conversion can be preceded by up to ~4.4 ms of
 * reference power-up right after a wake, hence 10 ms. */
#define ADC_MD          ISOSPI_MD_NORMAL
#define ADCV_WAIT_MS    10
#define ADSTAT_WAIT_MS  5
#define ADAX_WAIT_MS    10
#endif

/* Mux + op-amp buffer + 100 ohm/10 nF filter settle in ~1 us; 5 ms is pure
 * margin (and the shortest useful FreeRTOS delay at this tick rate). */
#define MUX_SETTLE_MS 5

/* The chip doesn't acknowledge writes, and one mux-select write right
 * after a wake was seen to go missing. Read back and retry this many times. */
#define SELECT_WRITE_ATTEMPTS 3

/* A select line measured above this is a 1 (pull-ups go to ~4.5 V; a line
 * the LTC6811 pulls down reads a few mV). */
#define SELECT_HIGH_MV 2000U

/* ---- Constants ----------------------------------------------------------- */

#define CELLS_PER_DEVICE 12
#define NO_RESULT        0xFFFFU   /* register value before any conversion */
#define CFGR0_REFON      (1U << 2)

/* Config register group A for the cell measurement:
 *   CFGR0 = 0xFC: GPIO pull-downs off (bits 7:3 = 1), REFON = 1 so the
 *          reference is already up when ADCV arrives, ADCOPT = 0 (so the
 *          MD codes mean 27 kHz / 7 kHz / 26 Hz).
 *   CFGR1-3 = 0: under/over-voltage thresholds unused here.
 *   CFGR4-5 = 0: every balancing switch off, discharge timer off. */
static const uint8_t config[6] = { 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* Thermistor table built from HW_THERMISTORS in hw.h. */
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

/* ---- Helpers ------------------------------------------------------------
 * TIP: reg16/print_line/the thermistor table are repeated in each test
 * file so every test reads on its own. In your rewrite, put them in one
 * shared LTC6811 module instead. */

static void print_line(const char *line)
{
    fputs(line, stdout);
}

/* Register groups hold three 16-bit values, LOW byte first. */
static uint16_t reg16(const uint8_t group[6], int index)
{
    return (uint16_t)(group[index * 2] | (group[index * 2 + 1] << 8));
}

/* CFGR0 that selects mux `channel`: REFON = 1, GPIO1/GPIO2 (bits 3/4)
 * pull-downs off since they're the analog inputs, plus the select lines. */
static uint8_t cfgr0_for_channel(int channel)
{
    return (uint8_t)(CFGR0_REFON | (1U << 3) | (1U << 4) | HW_THERM_SELECT_BITS(channel));
}

/* Appends a 100 uV/count code as " mmmm.m" (millivolts), or "    ----" if
 * the register was never written. Integer math only -- no float printf
 * needed (some embedded printf builds don't support %f at all). */
static int append_mv(char *line, int len, size_t size, uint16_t code)
{
    if (len >= (int)size)
    {
        return len;
    }
    if (code == NO_RESULT)
    {
        return len + snprintf(line + len, size - (size_t)len, "    ----");
    }
    return len + snprintf(line + len, size - (size_t)len, " %5u.%u",
                          (unsigned)(code / 10U), (unsigned)(code % 10U));
}

/* Appends one thermistor line, e.g.
 *   "   TH6  MUX1 ->   955.2 mV -> R =  10263 ohm -> 24.3 C"
 * No newline -- the caller may add a flag. */
static int append_therm(char *line, int len, size_t size, const char *label,
                        int mux, uint16_t v_code, uint16_t ref_code)
{
    if (len >= (int)size)
    {
        return len;
    }

    len += snprintf(line + len, size - (size_t)len, "   %-4s MUX%d ->", label, mux);
    len = append_mv(line, len, size, v_code);
    if (len >= (int)size)
    {
        return len;
    }

    float r = 0.0f;
    float temp_c = 0.0f;
    switch (thermistor_from_divider(v_code, ref_code, &r, &temp_c))
    {
    case THERM_NO_DATA:
        return len + snprintf(line + len, size - (size_t)len, " mV -> no result");
    case THERM_MISSING:
        return len + snprintf(line + len, size - (size_t)len, " mV -> thermistor missing");
    case THERM_SHORT:
        return len + snprintf(line + len, size - (size_t)len, " mV -> SHORT (at GND)");
    case THERM_OFF_CURVE:
        return len + snprintf(line + len, size - (size_t)len, " mV -> R = %6ld ohm -> off the curve",
                              lroundf(r));
    case THERM_OK:
        break;
    }

    /* Print tenths of a degree as integers (see append_mv). */
    long t_dc = lroundf(temp_c * 10.0f);
    long t_abs = t_dc < 0 ? -t_dc : t_dc;

    len += snprintf(line + len, size - (size_t)len, " mV -> R = %6ld ohm -> %s%ld.%ld C",
                    lroundf(r), t_dc < 0 ? "-" : "", t_abs / 10, t_abs % 10);
    if (temp_c > THERM_SHORT_ABOVE_C && len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, "  <-- SHORT? (over %d C)",
                        THERM_SHORT_ABOVE_C);
    }
    return len;
}

/* ---- Task ----------------------------------------------------------------- */

void cell_test_task(void *argument)
{
    (void)argument;

    isospi_init();

    /* Short pause so the first printf doesn't race the flash tool's
     * debug-port disconnect (matters for the semihosting build). */
    vTaskDelay(pdMS_TO_TICKS(300));

#if CELL_TEST_MEASURE_CELLS
    static const uint16_t rdcv_cmds[4] = {
        ISOSPI_CMD_RDCVA, ISOSPI_CMD_RDCVB, ISOSPI_CMD_RDCVC, ISOSPI_CMD_RDCVD,
    };

    uint8_t group[HW_ISOSPI_DEVICE_COUNT][6];
    uint16_t cell_code[HW_ISOSPI_DEVICE_COUNT][CELLS_PER_DEVICE];
    uint8_t stat_a[HW_ISOSPI_DEVICE_COUNT][6];
    uint8_t stat_b[HW_ISOSPI_DEVICE_COUNT][6];
#endif
    uint8_t cfg[HW_ISOSPI_DEVICE_COUNT][6];
    uint16_t therm_code[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT];
    uint16_t therm_ref[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT];
    /* Select-line voltages seen during each thermistor's conversion, as
     * A2/A1/A0 (= GPIO5/GPIO4/GPIO3). ADAX measures them for free, which
     * lets the report prove the mux was on the intended channel. */
    uint16_t therm_sel[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT][3];
    unsigned run_count = 0;

#if THERM_HOLD_CHANNEL >= 0
    /* Hold mode: rewrite the config (REFON + the held channel) and convert
     * every ~0.5 s -- inside the 2 s watchdog, so the chip never sleeps
     * and VREF2 stays up for probing. Never returns. */
    led_set(HW_LED_ACTIVE, 1);
    for (;;)
    {
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            for (int i = 0; i < 6; i++)
            {
                cfg[dev][i] = config[i];
            }
            cfg[dev][0] = cfgr0_for_channel(THERM_HOLD_CHANNEL);
        }
        isospi_wakeup();
        isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);

        isospi_send_cmd(ISOSPI_CMD_ADAX(ADC_MD));
        vTaskDelay(pdMS_TO_TICKS(ADAX_WAIT_MS));
        isospi_wakeup();

        uint8_t aux_a[HW_ISOSPI_DEVICE_COUNT][6];
        uint8_t aux_b[HW_ISOSPI_DEVICE_COUNT][6];
        int pec_failures = isospi_read_group(ISOSPI_CMD_RDAUXA, aux_a, HW_ISOSPI_DEVICE_COUNT);
        pec_failures += isospi_read_group(ISOSPI_CMD_RDAUXB, aux_b, HW_ISOSPI_DEVICE_COUNT);
        led_set(HW_LED_FAULT, pec_failures != 0);

        static char hold_line[HW_ISOSPI_DEVICE_COUNT * 160 + 64];
        int len = 0;
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT && len < (int)sizeof(hold_line); dev++)
        {
            len += snprintf(hold_line + len, sizeof(hold_line) - (size_t)len,
                            "HOLD S%d dev%d: VREF2", THERM_HOLD_CHANNEL, dev);
            len = append_mv(hold_line, len, sizeof(hold_line), reg16(aux_b[dev], 2));
            if (len < (int)sizeof(hold_line))
            {
                len += snprintf(hold_line + len, sizeof(hold_line) - (size_t)len, " | GPIO1 (MUX1)");
            }
            len = append_mv(hold_line, len, sizeof(hold_line), reg16(aux_a[dev], 0));
            if (len < (int)sizeof(hold_line))
            {
                len += snprintf(hold_line + len, sizeof(hold_line) - (size_t)len, " | GPIO2 (MUX2)");
            }
            len = append_mv(hold_line, len, sizeof(hold_line), reg16(aux_a[dev], 1));
            if (len < (int)sizeof(hold_line))
            {
                len += snprintf(hold_line + len, sizeof(hold_line) - (size_t)len,
                                " mV%s\n", pec_failures ? "  (PEC FAIL)" : "");
            }
        }
        print_line(hold_line);
    }
#endif

    for (;;)
    {
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            for (int i = 0; i < 6; i++)
            {
                cfg[dev][i] = config[i];
            }
        }

        led_set(HW_LED_ACTIVE, 1);
        isospi_wakeup();

        /* Read the config BEFORE rewriting it: sleep resets it, so REFON
         * reading back 0 proves the chip really slept since the last cycle
         * (which wrote REFON = 1). A cheap way to verify sleep/wake. */
        uint8_t cfg_before[HW_ISOSPI_DEVICE_COUNT][6];
        int wake_pec_failures = isospi_read_group(ISOSPI_CMD_RDCFGA, cfg_before, HW_ISOSPI_DEVICE_COUNT);

        isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);

        int pec_failures = 0;
        int select_retries = 0;

        /* Let VREG/VREF2 settle after the wake before measuring anything. */
        vTaskDelay(pdMS_TO_TICKS(THERM_POWER_SETTLE_MS));

#if CELL_TEST_MEASURE_CELLS
        /* The settle delay above left the link quiet for a second. */
        isospi_wakeup();
        isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);

        isospi_send_cmd(ISOSPI_CMD_ADCV(ADC_MD));
        vTaskDelay(pdMS_TO_TICKS(ADCV_WAIT_MS));

        /* The isoSPI port drops to IDLE after ~5 ms with no traffic (the
         * chip keeps its results), so wake the link again before reading. */
        isospi_wakeup();

        for (int g = 0; g < 4; g++)
        {
            pec_failures += isospi_read_group(rdcv_cmds[g], group, HW_ISOSPI_DEVICE_COUNT);
            for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
            {
                for (int c = 0; c < 3; c++)
                {
                    cell_code[dev][g * 3 + c] = reg16(group[dev], c);
                }
            }
        }

        /* Status conversion: sum of cells, die temp, VA, VD. */
        isospi_send_cmd(ISOSPI_CMD_ADSTAT(ADC_MD));
        vTaskDelay(pdMS_TO_TICKS(ADSTAT_WAIT_MS));
        isospi_wakeup();
        pec_failures += isospi_read_group(ISOSPI_CMD_RDSTATA, stat_a, HW_ISOSPI_DEVICE_COUNT);
        pec_failures += isospi_read_group(ISOSPI_CMD_RDSTATB, stat_b, HW_ISOSPI_DEVICE_COUNT);
#endif

        /* Thermistors: for each mux channel in use, select it on both
         * muxes, let it settle, then one ADAX converts GPIO1 (MUX1),
         * GPIO2 (MUX2) and VREF2 together. */
        for (int ch = 0; ch < HW_THERM_MUX_CHANNELS; ch++)
        {
            int used = 0;
            for (int t = 0; t < THERM_COUNT; t++)
            {
                used |= therms[t].channel == ch;
            }
            if (!used)
            {
                continue;
            }

            for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
            {
                cfg[dev][0] = cfgr0_for_channel(ch);
            }

            /* Write the select bits, read them back, retry until they
             * stick (writes aren't acknowledged). */
            for (int attempt = 0; attempt < SELECT_WRITE_ATTEMPTS; attempt++)
            {
                isospi_wakeup();
                isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);

                uint8_t cfg_rb[HW_ISOSPI_DEVICE_COUNT][6];
                int ok = isospi_read_group(ISOSPI_CMD_RDCFGA, cfg_rb, HW_ISOSPI_DEVICE_COUNT) == 0;
                for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
                {
                    ok &= (cfg_rb[dev][0] & HW_THERM_SELECT_MASK) == (cfg[dev][0] & HW_THERM_SELECT_MASK);
                }
                if (ok)
                {
                    break;
                }
                select_retries++;
            }
            vTaskDelay(pdMS_TO_TICKS(MUX_SETTLE_MS));

            isospi_send_cmd(ISOSPI_CMD_ADAX(ADC_MD));
            vTaskDelay(pdMS_TO_TICKS(ADAX_WAIT_MS));
            isospi_wakeup();

            uint8_t aux_a[HW_ISOSPI_DEVICE_COUNT][6];
            uint8_t aux_b[HW_ISOSPI_DEVICE_COUNT][6];
            pec_failures += isospi_read_group(ISOSPI_CMD_RDAUXA, aux_a, HW_ISOSPI_DEVICE_COUNT);
            pec_failures += isospi_read_group(ISOSPI_CMD_RDAUXB, aux_b, HW_ISOSPI_DEVICE_COUNT);

            for (int t = 0; t < THERM_COUNT; t++)
            {
                if (therms[t].channel != ch)
                {
                    continue;
                }
                for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
                {
                    /* AUXA = GPIO1, GPIO2, GPIO3; AUXB = GPIO4, GPIO5, VREF2.
                     * MUX n is read on GPIO n. */
                    therm_code[dev][t] = reg16(aux_a[dev], therms[t].mux - 1);
                    therm_ref[dev][t] = reg16(aux_b[dev], 2);
                    therm_sel[dev][t][0] = reg16(aux_b[dev], 1); /* A2 = GPIO5 */
                    therm_sel[dev][t][1] = reg16(aux_b[dev], 0); /* A1 = GPIO4 */
                    therm_sel[dev][t][2] = reg16(aux_a[dev], 2); /* A0 = GPIO3 */
                }
            }
        }

        run_count++;

        /* Fault LED: on if this reading had a comm error, cleared by the
         * next clean one. (A program halt lights it permanently instead --
         * see fault_halt().) */
        led_set(HW_LED_FAULT, pec_failures != 0 || wake_pec_failures != 0);

        /* ---- Report ---- */

        /* static: too big for the task stack at the full chip count. */
        static char line[HW_ISOSPI_DEVICE_COUNT * (640 + THERM_COUNT * 192) + 256];
        int len = snprintf(line, sizeof(line),
                           "=== Reading %u (%s mode) -- PEC fails %d, mux select rewrites %d%s ===\n",
                           run_count, CELL_TEST_FILTERED ? "26 Hz filtered" : "7 kHz normal",
                           pec_failures, select_retries,
                           pec_failures ? " (some values below may be stale/garbage)" : "");

        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT && len < (int)sizeof(line); dev++)
        {
            const char *wake_state;
            if (wake_pec_failures)
            {
                wake_state = "unknown (config read failed PEC)";
            }
            else if (run_count == 1)
            {
                wake_state = (cfg_before[dev][0] & CFGR0_REFON) ? "was awake" : "was asleep (first reading)";
            }
            else
            {
                wake_state = (cfg_before[dev][0] & CFGR0_REFON)
                    ? "was STILL AWAKE -- did not sleep during the wait"
                    : "was asleep -- woke up OK";
            }
            len += snprintf(line + len, sizeof(line) - (size_t)len, "  dev%d %s\n", dev, wake_state);
        }

        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT && len < (int)sizeof(line); dev++)
        {
#if CELL_TEST_MEASURE_CELLS
            /* Cells numbered from 0 like the board check: C0 is the
             * LTC6811's first cell (its C1-C0 pins). */
            len += snprintf(line + len, sizeof(line) - (size_t)len, "  dev%d cells (mV):\n   ", dev);
            for (int c = 0; c < CELLS_PER_DEVICE; c++)
            {
                if (c == 6 && len < (int)sizeof(line))
                {
                    len += snprintf(line + len, sizeof(line) - (size_t)len, "\n   ");
                }
                if (len < (int)sizeof(line))
                {
                    len += snprintf(line + len, sizeof(line) - (size_t)len, "  C%-2d", c);
                }
                len = append_mv(line, len, sizeof(line), cell_code[dev][c]);
            }

            /* Status register scales (LTC6811 datasheet):
             *   SC (sum of cells): 100 uV * 20 = 2 mV per count -- the chip
             *     divides the stack by 20 so it fits the ADC range.
             *   ITMP (die temp):   T = code * 100 uV / 7.5 mV/C - 273, done
             *     here in tenths of a degree to stay in integers.
             *   VA / VD (analog/digital supplies): 100 uV per count. */
            uint16_t sc   = reg16(stat_a[dev], 0);
            uint16_t itmp = reg16(stat_a[dev], 1);
            uint16_t va   = reg16(stat_a[dev], 2);
            uint16_t vd   = reg16(stat_b[dev], 0);

            uint32_t sum_cells_mv = 0;
            for (int c = 0; c < CELLS_PER_DEVICE; c++)
            {
                if (cell_code[dev][c] != NO_RESULT)
                {
                    sum_cells_mv += cell_code[dev][c] / 10U;
                }
            }

            int32_t temp_dc = (int32_t)itmp * 10 / 75 - 2730;
            int32_t temp_abs = temp_dc < 0 ? -temp_dc : temp_dc;

            /* Two independent stack measurements: the 12 cells added up,
             * and the chip's own SC. They should roughly agree; a big gap
             * points at the V+ supply path or a cell input. */
            if (len < (int)sizeof(line))
            {
                len += snprintf(line + len, sizeof(line) - (size_t)len,
                                "\n   STACK TOTAL: %lu mV (12 cells added up) | chip's sum-of-cells (SC): %lu mV\n"
                                "   die temp: %s%ld.%ld C | VA (analog 5V): %u mV | VD (digital 3.3V): %u mV\n",
                                (unsigned long)sum_cells_mv, (unsigned long)sc * 2UL,
                                temp_dc < 0 ? "-" : "", (long)(temp_abs / 10), (long)(temp_abs % 10),
                                (unsigned)(va / 10U), (unsigned)(vd / 10U));
            }
#endif

            /* VREF2 should read ~3000 mV. It's measured by the same ADC as
             * everything else, so it doubles as an ADC sanity check -- it
             * read ~2890 mV while VREF1 and VREF2 were shorted on the board. */
            if (THERM_COUNT > 0 && len < (int)sizeof(line))
            {
                len += snprintf(line + len, sizeof(line) - (size_t)len, "  dev%d VREF2:", dev);
                len = append_mv(line, len, sizeof(line), therm_ref[dev][0]);
                if (len < (int)sizeof(line))
                {
                    len += snprintf(line + len, sizeof(line) - (size_t)len, " mV\n");
                }
            }
            for (int t = 0; t < THERM_COUNT; t++)
            {
                len = append_therm(line, len, sizeof(line), therms[t].label, therms[t].mux,
                                   therm_code[dev][t], therm_ref[dev][t]);

                /* Decode the measured select lines (A2, A1, A0) back into
                 * the channel the mux actually sat on during the
                 * conversion; only shown when it's wrong. */
                int seen = 0;
                for (int s = 0; s < 3; s++)
                {
                    seen = (seen << 1) | (therm_sel[dev][t][s] / 10U > SELECT_HIGH_MV);
                }
                if (seen != therms[t].channel && len < (int)sizeof(line))
                {
                    len += snprintf(line + len, sizeof(line) - (size_t)len,
                                    "  <-- SELECT MISMATCH: mux was on S%d, not S%d",
                                    seen, therms[t].channel);
                }
                if (len < (int)sizeof(line))
                {
                    len += snprintf(line + len, sizeof(line) - (size_t)len, "\n");
                }
            }
        }

        if (len < (int)sizeof(line))
        {
            snprintf(line + len, sizeof(line) - (size_t)len,
                     "Link quiet for %d s -- LTC6811 should go to sleep (VREG off)...\n\n",
                     SLEEP_WAIT_MS / 1000);
        }

        print_line(line);
        led_set(HW_LED_ACTIVE, 0);
        vTaskDelay(pdMS_TO_TICKS(SLEEP_WAIT_MS));
    }
}
