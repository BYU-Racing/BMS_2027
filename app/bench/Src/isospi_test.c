/*
 * isospi_test.c -- link bring-up test: write a known pattern into every
 * chip's config register, read it back, compare. See isospi_test.h.
 *
 * WHY THIS IS THE FIRST TEST: it exercises both directions of the link
 * (write and read) with no analog parts involved. If this passes, the pins,
 * SPI mode, LTC6820 and isoSPI wiring are all right, and any later
 * measurement problem is analog, not communication.
 *
 * Reading the output:
 *   PEC fails > 0           -> the read data was corrupted (or nothing
 *                              answered: all FF).
 *   PEC ok, mismatches > 0  -> the chip answered but the write didn't take
 *                              (bad command PEC, or chip asleep).
 *   "MISO low this run: no" -> MISO never pulled low for a single bit --
 *                              nothing at all is coming back.
 */

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "hw.h"
#include "isospi.h"
#include "isospi_test.h"

/* Which CFGR0 bits read back what was written. GPIO5..1 (bits 7:3) read
 * the actual pin level rather than the pull-down setting, and DTEN (bit 1)
 * is read-only, so only REFON (bit 2) and ADCOPT (bit 0) are compared.
 * TIP: always check the datasheet's register map for read-only or
 * "reads something different" bits before comparing a readback. */
#define CFGR0_COMPARE_MASK 0x05U

/* Two alternating test patterns. Alternating matters: a chip that missed
 * the write would still return the previous pattern, which then fails.
 *   CFGR0:   GPIO pull-downs off (bits 7:3 = 1), REFON = 1, ADCOPT toggles.
 *   CFGR1-3: the under/over-voltage thresholds -- harmless places to put
 *            arbitrary bit patterns (0x5A/0xA5 alternate every bit).
 *   CFGR4-5: 0, so no balancing switch is ever turned on.
 * REFON = 1 also puts ~3.0 V on the LTC6811's VREF2 pin, so a multimeter
 * can confirm writes are landing even when readback is broken. */
static const uint8_t patterns[2][6] = {
    { 0xFD, 0x5A, 0xA5, 0x5A, 0x00, 0x00 }, /* REFON=1, ADCOPT=1 */
    { 0xFC, 0xA5, 0x5A, 0xA5, 0x00, 0x00 }, /* REFON=1, ADCOPT=0 */
};

/* Only the selected bench task prints. Leave scheduling enabled so the
 * independent heartbeat can run during a report. */
static void print_line(const char *line)
{
    fputs(line, stdout);
}

static int group_matches(const uint8_t wrote[6], const uint8_t read[6])
{
    if ((wrote[0] & CFGR0_COMPARE_MASK) != (read[0] & CFGR0_COMPARE_MASK))
    {
        return 0;
    }
    for (int i = 1; i < 6; i++)
    {
        if (wrote[i] != read[i])
        {
            return 0;
        }
    }
    return 1;
}

static int all_ones(const uint8_t *buf, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (buf[i] != 0xFF)
        {
            return 0;
        }
    }
    return 1;
}

/* Appends "<label> XX XX XX ...\n" to line at offset len; returns the new
 * length. Every append helper in these tests checks len against size first
 * -- snprintf returns how much it WANTED to write, so len can run past the
 * buffer and must never be used to index beyond it. */
static int append_hex(char *line, int len, size_t size, const char *label,
                      const uint8_t *buf, int n)
{
    if (len >= (int)size)
    {
        return len;
    }
    len += snprintf(line + len, size - (size_t)len, "%s", label);
    for (int i = 0; i < n && len < (int)size; i++)
    {
        len += snprintf(line + len, size - (size_t)len, " %02X", buf[i]);
    }
    if (len < (int)size)
    {
        len += snprintf(line + len, size - (size_t)len, "\n");
    }
    return len;
}

void isospi_test_task(void *argument)
{
    (void)argument;

    isospi_init();

    /* Short pause so the first printf doesn't race the flash tool's
     * debug-port disconnect (matters for the semihosting build). */
    vTaskDelay(pdMS_TO_TICKS(300));

    uint8_t wrote[HW_ISOSPI_DEVICE_COUNT][6];
    uint8_t read[HW_ISOSPI_DEVICE_COUNT][6];
    unsigned pass_count = 0;
    unsigned run_count = 0;
    unsigned miso_low_count = 0;

    for (int p = 0; ; p ^= 1)
    {
        /* XOR the chip index into CFGR3 so each chip gets distinct data --
         * a chain that delivers groups to the wrong chip then fails. */
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            for (int i = 0; i < 6; i++)
            {
                wrote[dev][i] = patterns[p][i];
            }
            wrote[dev][3] ^= (uint8_t)dev;
        }

        isospi_wakeup();
        isospi_write_group(ISOSPI_CMD_WRCFGA, wrote, HW_ISOSPI_DEVICE_COUNT);

        /* Copy the write frame's raw bytes before the read overwrites the
         * driver's frame log. */
        const uint8_t *tx;
        const uint8_t *rx;
        int wr_len = isospi_last_frame(&tx, &rx);
        uint8_t wr_tx[ISOSPI_MAX_FRAME_LEN];
        uint8_t wr_rx[ISOSPI_MAX_FRAME_LEN];
        for (int i = 0; i < wr_len; i++)
        {
            wr_tx[i] = tx[i];
            wr_rx[i] = rx[i];
        }

        int pec_failures = isospi_read_group(ISOSPI_CMD_RDCFGA, read, HW_ISOSPI_DEVICE_COUNT);
        int rd_len = isospi_last_frame(&tx, &rx);

        /* Did MISO go low for even one bit anywhere in either frame? If
         * never, nothing is answering -- a wiring/power problem. */
        int miso_low = !all_ones(wr_rx, wr_len) || !all_ones(rx, rd_len);
        if (miso_low)
        {
            miso_low_count++;
        }

        int mismatches = 0;
        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT; dev++)
        {
            mismatches += !group_matches(wrote[dev], read[dev]);
        }

        run_count++;
        if (pec_failures == 0 && mismatches == 0)
        {
            pass_count++;
        }

        /* static: too big for the task stack at the full chip count. Only
         * this task uses it. */
        static char line[HW_ISOSPI_DEVICE_COUNT * 96 + ISOSPI_MAX_FRAME_LEN * 12 + 256];
        int len = snprintf(line, sizeof(line),
                           "isoSPI CFGA write/read, pattern %c: %s (PEC fails %d, mismatches %d) [%u/%u passed]\n",
                           'A' + p,
                           (pec_failures == 0 && mismatches == 0) ? "PASS" : "FAIL",
                           pec_failures, mismatches, pass_count, run_count);

        for (int dev = 0; dev < HW_ISOSPI_DEVICE_COUNT && len < (int)sizeof(line); dev++)
        {
            len += snprintf(line + len, sizeof(line) - (size_t)len,
                            "  dev%d %s wrote: %02X %02X %02X %02X %02X %02X\n"
                            "            read: %02X %02X %02X %02X %02X %02X\n",
                            dev, group_matches(wrote[dev], read[dev]) ? "ok " : "BAD",
                            wrote[dev][0], wrote[dev][1], wrote[dev][2],
                            wrote[dev][3], wrote[dev][4], wrote[dev][5],
                            read[dev][0], read[dev][1], read[dev][2],
                            read[dev][3], read[dev][4], read[dev][5]);
        }

        len = append_hex(line, len, sizeof(line), "  WRCFGA MOSI:", wr_tx, wr_len);
        len = append_hex(line, len, sizeof(line), "         MISO:", wr_rx, wr_len);
        len = append_hex(line, len, sizeof(line), "  RDCFGA MOSI:", tx, rd_len);
        len = append_hex(line, len, sizeof(line), "         MISO:", rx, rd_len);
        if (len < (int)sizeof(line))
        {
            snprintf(line + len, sizeof(line) - (size_t)len,
                     "  MISO low this run: %s  [any low bit in %u/%u runs]\n\n",
                     miso_low ? "YES" : "no", miso_low_count, run_count);
        }

        print_line(line);
        vTaskDelay(pdMS_TO_TICKS(1000)); /* stays inside the chip's 2 s watchdog */
    }
}
