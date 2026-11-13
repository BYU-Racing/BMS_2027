#ifndef ISOSPI_H
#define ISOSPI_H

#include <stdint.h>

#include "hw.h"

/*
 * isospi.h -- driver for talking to a daisy chain of LTC6811-1 battery
 * monitors through an LTC6820 isoSPI transceiver.
 *
 * HOW THE LINK WORKS (read this before the code):
 *   STM32 SPI2 --plain SPI--> LTC6820 --isoSPI pulses over a twisted
 *   pair--> LTC6811 #0 port A, whose port B continues to #1, and so on.
 *   The LTC6820 turns SPI bits into isoSPI pulses and back; to the firmware
 *   it looks like ordinary SPI with CS.
 *
 * EVERY TRANSACTION looks like this (LTC6811 datasheet, "Serial
 * Programming"):
 *   CS low -> 2-byte command -> 2-byte PEC -> [data] -> CS high
 *   - Commands with no data (start a conversion, clear registers): just
 *     the 4 bytes.
 *   - Writes: then 6 data bytes + 2 PEC bytes per chip.
 *   - Reads: the chips answer with 6 data bytes + 2 PEC bytes each.
 *   PEC is a 15-bit CRC that protects every command and every data block.
 *   A wrong PEC on a command or write is silently ignored by the chip; a
 *   wrong PEC on read data is how we detect a corrupted link.
 *
 * RULES THAT BIT US DURING BRING-UP:
 *   - The chips SLEEP after ~2 s with no valid command (their watchdog),
 *     which also resets their config. The isoSPI port itself goes IDLE
 *     after ~5 ms with no traffic. Call isospi_wakeup() before talking to
 *     a chain that has been quiet.
 *   - Writes have no acknowledgement. If a write matters (mux select,
 *     balancing), read the register back and compare.
 *   - All-0xFF reads mean nothing answered: wrong pins, wrong SPI mode, or
 *     the LTC6811 isn't powered/awake. See the README's bring-up notes.
 */

/* ---- LTC6811 command codes (datasheet "Command Codes" table) ------------ */

#define ISOSPI_CMD_WRCFGA  0x0001  /* write config register group A */
#define ISOSPI_CMD_RDCFGA  0x0002  /* read config register group A */
#define ISOSPI_CMD_RDCVA   0x0004  /* cells 1-3   (our cells 0-2) */
#define ISOSPI_CMD_RDCVB   0x0006  /* cells 4-6   (our cells 3-5) */
#define ISOSPI_CMD_RDCVC   0x0008  /* cells 7-9   (our cells 6-8) */
#define ISOSPI_CMD_RDCVD   0x000A  /* cells 10-12 (our cells 9-11) */
#define ISOSPI_CMD_RDAUXA  0x000C  /* GPIO1, GPIO2, GPIO3 */
#define ISOSPI_CMD_RDAUXB  0x000E  /* GPIO4, GPIO5, VREF2 */
#define ISOSPI_CMD_RDSTATA 0x0010  /* sum of cells, die temp, VA */
#define ISOSPI_CMD_RDSTATB 0x0012  /* VD, OV/UV flags */

/* Clear the cell / aux result registers to 0xFFFF. A read that then comes
 * back 0xFFFF means "no conversion has finished since" -- a cheap way to
 * catch a too-short conversion wait instead of silently reading old data. */
#define ISOSPI_CMD_CLRCELL 0x0711
#define ISOSPI_CMD_CLRAUX  0x0712

/* Conversion commands. The ADC mode `md` trades speed for noise (with
 * ADCOPT = 0 in the config, which is what all our tests write):
 *   md = 1: 27 kHz "fast"     -- ~1.1 ms for all cells, noisiest
 *   md = 2: 7 kHz  "normal"   -- ~2.3 ms for all cells
 *   md = 3: 26 Hz  "filtered" -- ~201 ms for all cells, quietest
 * All of these convert every cell / every aux channel, with discharge not
 * permitted during the conversion (DCP = 0).
 *   ADCV   -- the 12 cell voltages
 *   ADAX   -- GPIO1-5 and VREF2 (our thermistors are on GPIO1/GPIO2)
 *   ADCVAX -- the 12 cells AND GPIO1/GPIO2 in one conversion
 *   ADSTAT -- sum of cells, die temperature, analog/digital supplies
 * TIP: these are bit fields -- the datasheet's command table shows where
 * MD, DCP and the channel-select bits sit. Building them with a macro
 * avoids a page of magic numbers. */
#define ISOSPI_CMD_ADCV(md)    (0x0260U | ((unsigned)(md) << 7))
#define ISOSPI_CMD_ADAX(md)    (0x0460U | ((unsigned)(md) << 7))
#define ISOSPI_CMD_ADCVAX(md)  (0x046FU | ((unsigned)(md) << 7))
#define ISOSPI_CMD_ADSTAT(md)  (0x0468U | ((unsigned)(md) << 7))

#define ISOSPI_MD_FAST      1
#define ISOSPI_MD_NORMAL    2
#define ISOSPI_MD_FILTERED  3

/* Longest frame on the link: 4 command bytes (command + PEC), then 6 data
 * + 2 PEC bytes per chip. */
#define ISOSPI_MAX_FRAME_LEN (4 + 8 * HW_ISOSPI_DEVICE_COUNT)

/* ---- API ---------------------------------------------------------------- */

/* Configures SPI2 and the CS/SCK/MISO/MOSI pins from hw.h: 500 kHz, SPI
 * mode 0 (matching the LTC6820's POL/PHA straps, see hw.h). Call once. */
void isospi_init(void);

/* Wakes every chip in the chain from SLEEP/IDLE. Call before a command if
 * the chain has been quiet for more than ~4 ms. Blocks the calling task
 * ~2 ms per chip (uses vTaskDelay, so only call it from a task). */
void isospi_wakeup(void);

/* The 15-bit CRC ("PEC") the LTC681x uses on commands and data. Returns the
 * value already shifted into wire format, ready to send high byte first. */
uint16_t isospi_pec15(const uint8_t *data, int len);

/* Sends a command with no data (e.g. start a conversion). Every chip on
 * the chain sees it, so one call starts all of them at once. */
void isospi_send_cmd(uint16_t cmd);

/* Sends a write command followed by one 6-byte register group (+ PEC) per
 * chip. data[0] goes to the first chip on the chain (the same order
 * isospi_read_group returns). No acknowledgement -- read back to confirm. */
void isospi_write_group(uint16_t cmd, uint8_t data[][6], int device_count);

/* Sends a read command and clocks back one 6-byte register group per chip
 * (data[0] = first chip). Returns how many chips' PEC failed to check out:
 * 0 means the whole chain answered cleanly. */
int isospi_read_group(uint16_t cmd, uint8_t data[][6], int device_count);

/* Debug view of the most recent CS-low frame: *tx points at every byte
 * sent on MOSI, *rx at every byte received on MISO at the same time
 * (command bytes included). Returns the byte count. Valid until the next
 * call into this driver. This is what found the "MISO never goes low"
 * symptom during bring-up -- worth keeping a hook like it in your rewrite. */
int isospi_last_frame(const uint8_t **tx, const uint8_t **rx);

#endif /* ISOSPI_H */
