/*
 * board_check.c -- the everyday "is this slave board OK?" test. See
 * board_check.h.
 *
 * RUGBY INPUT (requested behavior):
 *   - streamlined output: every cell voltage, the stack total, and every
 *     thermistor in degrees C;
 *   - TH<n> printed on the same line as cell n, leftover thermistors at the
 *     bottom;
 *   - thermistors shown as "SHORT" / "NOT CONNECTED" when faulted;
 *   - sample, then let the boards sleep, then sample again.
 *
 * Each check:  wake -> 1 s settle -> cells -> 8 mux channels -> print ->
 *              6 s of silence (every LTC6811 sleeps).
 * Accuracy over speed: 26 Hz filtered conversions, ~5 s per check. For
 * speed numbers see fast_poll.c instead.
 */

#include <math.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "hw.h"
#include "isospi.h"
#include "led.h"
#include "thermistor.h"
#include "board_check.h"

/* ---- Knobs ------------------------------------------------------------- */

/* Filtered mode: quietest readings, ~201 ms per conversion. */
#define ADC_MD            ISOSPI_MD_FILTERED
#define ADCV_WAIT_MS      210   /* ~201 ms + margin */
#define ADAX_WAIT_MS      420   /* generous; the slow test never saw it come up short */

/* RUGBY INPUT: wait after a wake from sleep before sampling, so VREG and
 * VREF2 (which feeds every thermistor divider) are at steady state. */
#define POWER_SETTLE_MS   1000

/* Mux + buffer + 100 ohm/10 nF filter settle in ~1 us; this is margin. */
#define MUX_SETTLE_MS     5

/* RUGBY INPUT: silence after each check -- past the LTC6811's ~2 s
 * watchdog, so every board drops to sleep (VREG/VREF2 off) between checks. */
#define SLEEP_WAIT_MS     6000

/* Writes aren't acknowledged; read the mux select back and retry. */
#define SELECT_WRITE_ATTEMPTS 3

/* ---- Constants ----------------------------------------------------------- */

#define CELLS_PER_DEVICE  12
#define NO_RESULT         0xFFFFU   /* register value after a clear / before a conversion */

/* CFGR0 base: REFON = 1, GPIO1/GPIO2 pull-downs off (they're the analog
 * thermistor inputs); the mux select bits are ORed in per channel.
 * CFGR1-5 = 0: no under/over-voltage thresholds, every balancing switch off. */
#define CFGR0_BASE ((uint8_t)((1U << 2) | (1U << 3) | (1U << 4)))

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

/* Latest readings, raw ADC codes (100 uV per count). */
static uint16_t cell_code[HW_ISOSPI_DEVICE_COUNT][CELLS_PER_DEVICE];
static uint16_t therm_code[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT];
static uint16_t therm_ref[HW_ISOSPI_DEVICE_COUNT][THERM_COUNT];   /* VREF2 from the same conversion */
static int comm_errors;

/* ---- Helpers (see the tip in cell_test.c about sharing these) ----------- */

/* Register groups hold three 16-bit values, LOW byte first. */
static uint16_t reg16(const uint8_t group[6], int index)
{
    return (uint16_t)(group[index * 2] | (group[index * 2 + 1] << 8));
}

static void print_line(const char *line)
{
    fputs(line, stdout);
}

/* "TH7" -> 7; anything not "TH<digits>" -> -1. This is how a thermistor
 * gets lined up with its cell, so labels in hw.h must follow "TH<n>". */
static int therm_number(const char *label)
{
    if (label[0] != 'T' || label[1] != 'H' || label[2] < '0' || label[2] > '9')
    {
        return -1;
    }
    int n = 0;
    for (const char *p = label + 2; *p; p++)
    {
        if (*p < '0' || *p > '9')
        {
            return -1;
        }
        n = n * 10 + (*p - '0');
    }
    return n;
}

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

/* ---- Measurement ----------------------------------------------------------- */

/* Selects mux `ch` on every board. Also the config write that sets REFON,
 * and (like any valid command) it resets each chip's watchdog. Reads the
 * config back and rewrites until the select bits stick: one write right
 * after a wake was seen to go missing during bring-up. */
static void select_channel(int ch)
{
    uint8_t cfg[HW_ISOSPI_DEVICE_COUNT][6] = { 0 };
    for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
    {
        cfg[dev][0] = (uint8_t)(CFGR0_BASE | HW_THERM_SELECT_BITS(ch));
    }

    for (int attempt = 0; attempt < SELECT_WRITE_ATTEMPTS; attempt++)
    {
        isospi_wakeup();
        isospi_write_group(ISOSPI_CMD_WRCFGA, cfg, HW_ISOSPI_DEVICE_COUNT);

        uint8_t rb[HW_ISOSPI_DEVICE_COUNT][6];
        int ok = isospi_read_group(ISOSPI_CMD_RDCFGA, rb, HW_ISOSPI_DEVICE_COUNT) == 0;
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            ok &= (rb[dev][0] & HW_THERM_SELECT_MASK) == (cfg[dev][0] & HW_THERM_SELECT_MASK);
        }
        if (ok)
        {
            return;
        }
    }
    comm_errors++;
}

static void measure_cells(void)
{
    static const uint16_t rdcv[4] = {
        ISOSPI_CMD_RDCVA, ISOSPI_CMD_RDCVB, ISOSPI_CMD_RDCVC, ISOSPI_CMD_RDCVD,
    };

    /* Clear first, so a conversion that didn't finish reads 0xFFFF
     * (shown as "?") instead of last time's value. */
    isospi_wakeup();
    isospi_send_cmd(ISOSPI_CMD_CLRCELL);
    isospi_send_cmd(ISOSPI_CMD_ADCV(ADC_MD));
    vTaskDelay(pdMS_TO_TICKS(ADCV_WAIT_MS));
    isospi_wakeup(); /* the link idled during the 210 ms wait */

    /* 12 cells come back as 4 register groups of 3. */
    for (int g = 0; g < 4; g++)
    {
        uint8_t group[HW_ISOSPI_DEVICE_COUNT][6];
        comm_errors += isospi_read_group(rdcv[g], group, HW_ISOSPI_DEVICE_COUNT);
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            for (int c = 0; c < 3; c++)
            {
                cell_code[dev][g * 3 + c] = reg16(group[dev], c);
            }
        }
    }
}

/* One ADAX per mux channel converts both muxes (GPIO1/GPIO2) and VREF2 at
 * once -- 8 conversions cover all 16 thermistors. */
static void measure_thermistors(void)
{
    for (int ch = 0; ch < HW_THERM_MUX_CHANNELS; ch++)
    {
        if (!channel_used(ch))
        {
            continue;
        }

        select_channel(ch);
        vTaskDelay(pdMS_TO_TICKS(MUX_SETTLE_MS));
        isospi_send_cmd(ISOSPI_CMD_CLRAUX);
        isospi_send_cmd(ISOSPI_CMD_ADAX(ADC_MD));
        vTaskDelay(pdMS_TO_TICKS(ADAX_WAIT_MS));
        isospi_wakeup();

        uint8_t aux_a[HW_ISOSPI_DEVICE_COUNT][6];
        uint8_t aux_b[HW_ISOSPI_DEVICE_COUNT][6];
        comm_errors += isospi_read_group(ISOSPI_CMD_RDAUXA, aux_a, HW_ISOSPI_DEVICE_COUNT);
        comm_errors += isospi_read_group(ISOSPI_CMD_RDAUXB, aux_b, HW_ISOSPI_DEVICE_COUNT);

        for (int t = 0; t < THERM_COUNT; t++)
        {
            if (therms[t].channel != ch)
            {
                continue;
            }
            for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
            {
                therm_code[dev][t] = reg16(aux_a[dev], therms[t].mux - 1); /* GPIO1 or GPIO2 */
                therm_ref[dev][t] = reg16(aux_b[dev], 2);                  /* VREF2 */
            }
        }
    }
}

/* ---- Report ---------------------------------------------------------------- */

/* "24.3 C", "NOT CONNECTED", "SHORT", or "?" (no data / off the curve). */
static void format_temp(char *buf, size_t size, uint16_t v_code, uint16_t ref_code)
{
    float r = 0.0f;
    float temp_c = 0.0f;
    switch (thermistor_from_divider(v_code, ref_code, &r, &temp_c))
    {
    case THERM_OK:
    {
        /* Integer tenths -- avoids needing float support in printf. */
        long t_dc = lroundf(temp_c * 10.0f);
        long t_abs = t_dc < 0 ? -t_dc : t_dc;
        snprintf(buf, size, "%s%ld.%ld C", t_dc < 0 ? "-" : "", t_abs / 10, t_abs % 10);
        break;
    }
    case THERM_MISSING:
        snprintf(buf, size, "NOT CONNECTED");
        break;
    case THERM_SHORT:
        snprintf(buf, size, "SHORT");
        break;
    default:
        snprintf(buf, size, "?");
        break;
    }
}

/* One board's block:
 *    Slave 0
 *     C0  3.4011 V   TH0  24.3 C
 *     ...
 *     Stack 40.979 V
 *     TH12 NOT CONNECTED   TH13 ...   */
static int append_board(char *line, int len, size_t size, int dev)
{
    if (len >= (int)size)
    {
        return len;
    }
    len += snprintf(line + len, size - (size_t)len, " Slave %d\n", dev);

    /* One row per cell, with the same-numbered thermistor beside it.
     * Cells are numbered from 0: C0 is the LTC6811's first cell (C1-C0
     * pins), matching the TH numbering. */
    uint32_t stack = 0;
    int stack_valid = 1;
    for (int c = 0; c < CELLS_PER_DEVICE && len < (int)size; c++)
    {
        uint16_t code = cell_code[dev][c];
        if (code == NO_RESULT)
        {
            stack_valid = 0;
            len += snprintf(line + len, size - (size_t)len, "  C%-2d     ?    ", c);
        }
        else
        {
            /* 100 uV per count -> volts with 4 decimals, integer math. */
            stack += code;
            len += snprintf(line + len, size - (size_t)len, "  C%-2d %u.%04u V", c,
                            code / 10000U, code % 10000U);
        }

        for (int t = 0; t < THERM_COUNT && len < (int)size; t++)
        {
            if (therm_number(therms[t].label) == c)
            {
                char temp[16];
                format_temp(temp, sizeof(temp), therm_code[dev][t], therm_ref[dev][t]);
                len += snprintf(line + len, size - (size_t)len, "   %-4s %s", therms[t].label, temp);
                break;
            }
        }
        if (len < (int)size)
        {
            len += snprintf(line + len, size - (size_t)len, "\n");
        }
    }

    /* Stack = the 12 cells added up (codes are 100 uV, so sum / 10 = mV). */
    if (len < (int)size)
    {
        if (stack_valid)
        {
            uint32_t mv = stack / 10U;
            len += snprintf(line + len, size - (size_t)len, "  Stack %lu.%03lu V\n",
                            (unsigned long)(mv / 1000U), (unsigned long)(mv % 1000U));
        }
        else
        {
            len += snprintf(line + len, size - (size_t)len, "  Stack ?\n");
        }
    }

    /* Thermistors without a matching cell (TH12-TH15), in number order --
     * a small insertion sort, since the table order is by mux, not number. */
    int extra[THERM_COUNT];
    int n_extra = 0;
    for (int t = 0; t < THERM_COUNT; t++)
    {
        int n = therm_number(therms[t].label);
        if (n < 0 || n >= CELLS_PER_DEVICE)
        {
            int i = n_extra++;
            while (i > 0 && therm_number(therms[extra[i - 1]].label) > n)
            {
                extra[i] = extra[i - 1];
                i--;
            }
            extra[i] = t;
        }
    }
    for (int i = 0; i < n_extra && len < (int)size; i++)
    {
        char temp[16];
        format_temp(temp, sizeof(temp), therm_code[dev][extra[i]], therm_ref[dev][extra[i]]);
        len += snprintf(line + len, size - (size_t)len, "%s%-4s %-13s", i == 0 ? "  " : "   ",
                        therms[extra[i]].label, temp);
    }
    if (n_extra && len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, "\n");
    }
    return len;
}

/* ---- Task -------------------------------------------------------------------- */

void board_check_task(void *argument)
{
    (void)argument;

    isospi_init();
    vTaskDelay(pdMS_TO_TICKS(300)); /* let the flash tool's debug disconnect finish */

    unsigned check = 0;
    for (;;)
    {
        led_set(HW_LED_ACTIVE, 1);
        comm_errors = 0;

        /* Wake every board, reference on (REFON in the config write), and
         * let the supplies settle. */
        isospi_wakeup();
        select_channel(0);
        vTaskDelay(pdMS_TO_TICKS(POWER_SETTLE_MS));

        measure_cells();
        measure_thermistors();
        check++;

        led_set(HW_LED_FAULT, comm_errors != 0);

        /* static: too big for the task stack at the full board count. */
        static char line[HW_ISOSPI_DEVICE_COUNT * 800 + 128];
        int len = snprintf(line, sizeof(line), "=== Board check %u ===\n", check);
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            len = append_board(line, len, sizeof(line), dev);
        }
        if (len < (int)sizeof(line))
        {
            if (comm_errors)
            {
                len += snprintf(line + len, sizeof(line) - (size_t)len,
                                "  COMM ERRORS: %d (values above may be stale)\n", comm_errors);
            }
            snprintf(line + len, sizeof(line) - (size_t)len, "  (sleeping %d s)\n\n",
                     SLEEP_WAIT_MS / 1000);
        }
        print_line(line);

        /* Go silent: no commands for SLEEP_WAIT_MS, so the watchdog puts
         * every LTC6811 to sleep. */
        led_set(HW_LED_ACTIVE, 0);
        vTaskDelay(pdMS_TO_TICKS(SLEEP_WAIT_MS));
    }
}
