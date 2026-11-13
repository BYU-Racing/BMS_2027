#ifndef HW_H
#define HW_H

#include <stdint.h>

#include "bench_gpio.h"

/*
 * hw.h -- the board description: every pin, wiring choice, and hardware
 * value the firmware depends on lives here, and nowhere else.
 *
 * WHY ONE FILE: when a board revision moves a pin or swaps a part, only this
 * file should change. Every other module refers to a name from here (e.g.
 * HW_ISOSPI_CS_PIN), never to a raw port/pin number. Keep that rule in your
 * rewrite -- the two worst bring-up bugs on this project (SCK and CS on the
 * wrong pins) would have been one-line fixes if the pins had been checked
 * against the schematic here first.
 *
 * CONVENTION: values marked "RUGBY INPUT" were supplied by Rugby from the
 * schematic, a datasheet, or a bench measurement. They are facts about the
 * 2027 hardware, not firmware choices -- re-verify each one against YOUR
 * board revision before trusting it (grep for "RUGBY INPUT").
 *
 * Target: STM32G474 (LQFP64) master board -> LTC6820 isoSPI transceiver ->
 * LTC6811-1 on each slave board.
 */

/* ===========================================================================
 * Status LEDs (master board)
 * ======================================================================== */

/* RUGBY INPUT (starter code): 7 LEDs on port A. */
#define HW_LED_PORT   GPIOA_BASE
#define HW_LED_COUNT  7
#define HW_LED0_PIN   0    /* PA0  -- board label LED1 */
#define HW_LED1_PIN   1    /* PA1  -- LED2 */
#define HW_LED2_PIN   2    /* PA2  -- LED3 */
#define HW_LED3_PIN   4    /* PA4  -- LED4 */
#define HW_LED4_PIN   5    /* PA5  -- LED5 */
#define HW_LED5_PIN   10   /* PA10 -- LED6 */
#define HW_LED6_PIN   15   /* PA15 -- LED7 */

/* What the LEDs mean. Indexes passed to led_set() are 0-based, so index 0
 * is the board's LED1 and index 6 is LED7.
 * RUGBY INPUT: LED2 = "measuring", LED7 = "fault", everything else off. */
#define HW_LED_HEARTBEAT 0   /* PA0: independent one-second heartbeat */
#define HW_LED_ACTIVE 1   /* LED2: on while a measurement is in progress */
#define HW_LED_FAULT  6   /* LED7: comm error (one cycle) or program halted (solid) */

/* ===========================================================================
 * isoSPI link: STM32 SPI2 -> LTC6820 (master board) -> twisted pair ->
 * LTC6811-1 port A (slave board) -> port B -> next slave ...
 * ======================================================================== */

/* RUGBY INPUT: pins from the master board schematic (LQFP64 pin numbers in
 * the comments).
 *
 * TIP: SCK/MISO/MOSI must be pins that can actually carry SPI2 -- check the
 * "alternate function" table in the STM32G474 datasheet. CS is just a GPIO
 * the driver toggles by hand, so it can be any free pin.
 *
 * History: the firmware originally drove SCK on PB13 and CS on PB12. The
 * board uses PF1 and PB6, so the LTC6820 never saw a clock and every read
 * returned 0xFF. If a link reads all-0xFF, check these four lines first. */
#define HW_ISOSPI_CS_PORT       GPIOB_BASE
#define HW_ISOSPI_CS_PIN        6   /* PB6,  pin 59 -- RUGBY INPUT */
#define HW_ISOSPI_SCK_PORT      GPIOF_BASE
#define HW_ISOSPI_SCK_PIN       1   /* PF1,  pin 6  -- RUGBY INPUT. Also the crystal's OSC_OUT pin:
                                     * free only because we run on the internal 16 MHz HSI. Adding
                                     * an external crystal on this board means moving SCK. */
#define HW_ISOSPI_MISO_PORT     GPIOB_BASE
#define HW_ISOSPI_MISO_PIN      14  /* PB14, pin 36 -- RUGBY INPUT */
#define HW_ISOSPI_MOSI_PORT     GPIOB_BASE
#define HW_ISOSPI_MOSI_PIN      15  /* PB15, pin 37 -- RUGBY INPUT */
#define HW_ISOSPI_AF            5   /* SPI2 is alternate function 5 on all three pins (datasheet AF table) */

/* RUGBY INPUT: the LTC6820's POL and PHA pins are tied to GND on the master
 * board, which means SPI mode 0 (clock idles low, sample on the rising
 * edge). The SPI setup in Src/isospi.c must match these straps -- a
 * mismatch also produces all-0xFF reads. Its MSTR and EN pins are tied
 * high (master mode, enabled). */

/* RUGBY INPUT: number of LTC6811-1 chips (= slave boards) on the chain.
 * 1 on the bench today; set to the slave count for the full pack. Every
 * buffer and loop in the firmware scales from this. */
#define HW_ISOSPI_DEVICE_COUNT  1

/* ===========================================================================
 * Slave board thermistors
 *
 * RUGBY INPUT (slave schematic):
 *   - Two TMUX1308 8:1 muxes (MUX1 = IC1, MUX2 = IC2) share their select
 *     lines A0/A1/A2, driven by LTC6811 GPIO3/GPIO4/GPIO5 -- so both muxes
 *     are always on the same channel.
 *   - MUX1's output goes through an op-amp buffer and a 100 ohm / 10 nF RC
 *     to LTC6811 GPIO1; MUX2's the same way to GPIO2.
 *   - Each thermistor is the bottom half of a divider: pull-up from VREF2
 *     (the LTC6811's 3.0 V reference output) to the mux input, thermistor
 *     from the mux input to V- (slave ground).
 *   - The select lines have external pull-ups.
 *
 * WHY THE PULL-UPS MATTER: LTC6811 GPIOs are open-drain -- writing a 0 to a
 * GPIO's config bit turns on a pull-down (line low); writing a 1 just lets
 * go. A select line only goes high because of its external pull-up.
 * ======================================================================== */

#define HW_THERM_MUX_CHANNELS   8   /* TMUX1308 is 8:1 */

/* LTC6811 config byte CFGR0 bits that select mux `channel`: A0/A1/A2 =
 * GPIO3/4/5 = CFGR0 bits 5/6/7 (CFGR0 bits 7..3 are GPIO5..GPIO1). OR this
 * into the rest of CFGR0. If the select wiring changes, change it here. */
#define HW_THERM_SELECT_MASK    0xE0U
#define HW_THERM_SELECT_BITS(channel) \
    ((uint8_t)((((channel) & 1U) << 5) | ((((channel) >> 1) & 1U) << 6) | ((((channel) >> 2) & 1U) << 7)))

/* RUGBY INPUT (measured on the board): thermistor divider pull-up, same on
 * every channel. The temperature math depends on this directly -- with the
 * wrong value a 10k resistor (25 C) read as 41-48 C until it was corrected
 * to the fitted 22k. If temperatures look consistently off, measure this
 * part before touching the code. */
#define HW_THERM_PULLUP_OHMS    22000.0f

/* RUGBY INPUT (slave schematic): which thermistor is on which mux input.
 * One line per thermistor:
 *
 *   X(label, mux, channel)
 *
 *   label   -- name printed in the reports; must be "TH<n>" for the board
 *              check to line TH<n> up with cell n
 *   mux     -- 1 = MUX1 (IC1, read on GPIO1), 2 = MUX2 (IC2, read on GPIO2)
 *   channel -- TMUX1308 input S0-S7, selected by A2A1A0 = channel in binary
 *
 * The comment on each line is the TMUX1308 package pin, for checking
 * against the board. Order doesn't matter; comment a line out to skip that
 * thermistor.
 *
 * TIP: this is an "X-macro" -- each test file defines X() to turn the list
 * into whatever it needs (here, an array of structs). It keeps the mapping
 * in one place. A plain const array in a .c file works just as well in
 * your rewrite. */
#define HW_THERMISTORS(X) \
    /* MUX1 (IC1) -> GPIO1 */                \
    X("TH1",  1, 0)   /* S0, pin 13 */       \
    X("TH2",  1, 1)   /* S1, pin 14 */       \
    X("TH3",  1, 2)   /* S2, pin 15 */       \
    X("TH4",  1, 3)   /* S3, pin 12 */       \
    X("TH5",  1, 4)   /* S4, pin 1  */       \
    X("TH6",  1, 5)   /* S5, pin 5  */       \
    X("TH0",  1, 6)   /* S6, pin 2  */       \
    X("TH14", 1, 7)   /* S7, pin 4  */       \
    /* MUX2 (IC2) -> GPIO2 */                \
    X("TH7",  2, 0)   /* S0, pin 13 */       \
    X("TH8",  2, 1)   /* S1, pin 14 */       \
    X("TH9",  2, 2)   /* S2, pin 15 */       \
    X("TH10", 2, 3)   /* S3, pin 12 */       \
    X("TH11", 2, 4)   /* S4, pin 1  */       \
    X("TH12", 2, 5)   /* S5, pin 5  */       \
    X("TH13", 2, 6)   /* S6, pin 2  */       \
    X("TH15", 2, 7)   /* S7, pin 4  */

/* ===========================================================================
 * Debug output
 *
 * RUGBY INPUT: SWO is wired to PB3 (pin 56) through the Tag-Connect. printf
 * goes out that pin by default (Src/swo.c). PB3 must not be reused for
 * anything else while SWO is in use.
 * ======================================================================== */

#endif /* HW_H */
