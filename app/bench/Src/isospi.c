/*
 * isospi.c -- LTC6820/LTC6811 isoSPI driver. See isospi.h for how the link
 * and its transactions work.
 *
 * The SPI peripheral is used in the simplest possible way: one byte at a
 * time, waiting on status flags (polling). No DMA, no interrupts. At 500
 * kHz a byte takes 16 us, so this is fine for a BMS. If you need more
 * speed later, the measured bottleneck was CPU time per byte at 16 MHz
 * (see the fast poll test), not the SPI itself.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "bench_gpio.h"
#include "hw.h"
#include "isospi.h"

/* ---- SPI2 registers (RM0440, "SPI registers") -------------------------- */

#define RCC_APB1ENR1        (*(volatile uint32_t *)0x40021058UL)
#define RCC_APB1ENR1_SPI2EN (1UL << 14)

#define SPI2_BASE        0x40003800UL
#define SPI2_CR1         (*(volatile uint32_t *)(SPI2_BASE + 0x00UL))
#define SPI2_CR2         (*(volatile uint32_t *)(SPI2_BASE + 0x04UL))
#define SPI2_SR          (*(volatile uint32_t *)(SPI2_BASE + 0x08UL))
/* The data register accessed as a BYTE. On STM32G4 a 32-bit access to DR
 * would push two bytes into the 8-bit FIFO -- the 8-bit pointer makes each
 * write exactly one SPI frame. Easy to get wrong. */
#define SPI2_DR8         (*(volatile uint8_t  *)(SPI2_BASE + 0x0CUL))

#define SPI_CR1_CPHA     (1UL << 0)   /* unused in mode 0; kept for switching to mode 3 */
#define SPI_CR1_CPOL     (1UL << 1)   /* unused in mode 0; kept for switching to mode 3 */
#define SPI_CR1_MSTR     (1UL << 2)
#define SPI_CR1_BR_DIV32 (4UL << 3)   /* 16 MHz PCLK1 / 32 = 500 kHz (the LTC6820 allows up to 1 MHz) */
#define SPI_CR1_SPE      (1UL << 6)
#define SPI_CR1_SSI      (1UL << 8)
#define SPI_CR1_SSM      (1UL << 9)
#define SPI_CR2_DS_8BIT  (7UL << 8)
#define SPI_CR2_FRXTH    (1UL << 12)  /* RXNE fires after 8 bits, not 16 */
#define SPI_SR_RXNE      (1UL << 0)
#define SPI_SR_TXE       (1UL << 1)

/* ---- Debug log of the current frame (see isospi_last_frame) ------------- */

static uint8_t log_tx[ISOSPI_MAX_FRAME_LEN];
static uint8_t log_rx[ISOSPI_MAX_FRAME_LEN];
static int log_len;

/* CS is driven by hand (not by the SPI peripheral) because one LTC6811
 * transaction spans many bytes and CS must stay low for all of them. */
static void cs_low(void)
{
    log_len = 0; /* a new frame starts at every CS falling edge */
    gpio_write(HW_ISOSPI_CS_PORT, HW_ISOSPI_CS_PIN, 0);
}

static void cs_high(void) { gpio_write(HW_ISOSPI_CS_PORT, HW_ISOSPI_CS_PIN, 1); }

/* Full-duplex byte exchange: SPI always receives one byte for every byte
 * sent. When reading, we send 0xFF as filler and keep what comes back. */
static uint8_t spi_xfer(uint8_t tx)
{
    while (!(SPI2_SR & SPI_SR_TXE)) {}
    SPI2_DR8 = tx;
    while (!(SPI2_SR & SPI_SR_RXNE)) {}
    uint8_t rx = SPI2_DR8;

    if (log_len < ISOSPI_MAX_FRAME_LEN)
    {
        log_tx[log_len] = tx;
        log_rx[log_len] = rx;
        log_len++;
    }
    return rx;
}

int isospi_last_frame(const uint8_t **tx, const uint8_t **rx)
{
    *tx = log_tx;
    *rx = log_rx;
    return log_len;
}

void isospi_init(void)
{
    /* Set CS's output level high BEFORE making it an output, so the
     * LTC6820 never sees a spurious CS-low glitch at startup.
     * TIP (hardware): a ~10k pull-up on CS keeps it high while the MCU is
     * in reset, before this code runs -- worth adding on the next rev. */
    gpio_clock_enable(HW_ISOSPI_CS_PORT);
    gpio_write(HW_ISOSPI_CS_PORT, HW_ISOSPI_CS_PIN, 1);
    gpio_set_mode(HW_ISOSPI_CS_PORT, HW_ISOSPI_CS_PIN, GPIO_MODE_OUTPUT);

    /* SCK/MISO/MOSI can sit on different ports (SCK is on port F), so each
     * pin carries its own port. */
    const struct { uint32_t port; uint8_t pin; } spi_pins[] = {
        { HW_ISOSPI_SCK_PORT,  HW_ISOSPI_SCK_PIN  },
        { HW_ISOSPI_MISO_PORT, HW_ISOSPI_MISO_PIN },
        { HW_ISOSPI_MOSI_PORT, HW_ISOSPI_MOSI_PIN },
    };
    for (unsigned i = 0; i < sizeof(spi_pins) / sizeof(spi_pins[0]); i++)
    {
        gpio_clock_enable(spi_pins[i].port);
        gpio_set_af(spi_pins[i].port, spi_pins[i].pin, HW_ISOSPI_AF);
        gpio_set_mode(spi_pins[i].port, spi_pins[i].pin, GPIO_MODE_AF);
    }

    RCC_APB1ENR1 |= RCC_APB1ENR1_SPI2EN;

    /* SPI mode 0 (CPOL = 0, CPHA = 0), master, 500 kHz.
     * RUGBY INPUT (hardware): mode 0 because the LTC6820's POL/PHA pins are
     * tied to GND. If a board rev ties them high, add
     * SPI_CR1_CPOL | SPI_CR1_CPHA here (mode 3). A mismatch makes every
     * command fail its PEC, so every read comes back 0xFF.
     * SSM/SSI: "software slave management" -- tells the peripheral it's
     * the master without needing a hardware NSS pin (we drive CS by hand). */
    SPI2_CR1 = SPI_CR1_MSTR | SPI_CR1_BR_DIV32 | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI2_CR2 = SPI_CR2_DS_8BIT | SPI_CR2_FRXTH;
    SPI2_CR1 |= SPI_CR1_SPE; /* enable last, after the configuration is complete */
}

void isospi_wakeup(void)
{
    /* Each LTC6811 wakes on a CS-low pulse (takes < ~400 us) and only then
     * forwards the next pulse down the chain, so pulse once per chip and
     * give each one time. The 0xFF byte is a throwaway -- it just makes the
     * LTC6820 generate activity on the link. */
    for (int i = 0; i < HW_ISOSPI_DEVICE_COUNT; i++)
    {
        cs_low();
        spi_xfer(0xFF);
        cs_high();
        vTaskDelay(pdMS_TO_TICKS(2)); /* 1 kHz tick: sleeps at least 1 ms */
    }
}

uint16_t isospi_pec15(const uint8_t *data, int len)
{
    /* CRC-15, polynomial 0x4599, seed 16, as given in the LTC6811
     * datasheet ("PEC Calculation"). Bit-by-bit for clarity.
     * TIP: the datasheet also gives a 256-entry lookup-table version --
     * noticeably faster at 16 MHz, worth using once speed matters.
     * Check your version against the datasheet's examples:
     * command RDCFGA (00 02) -> PEC 2B 0A, WRCFGA (00 01) -> 3D 6E. */
    uint16_t rem = 16; /* seed */

    for (int i = 0; i < len; i++)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            uint16_t in = ((rem >> 14) & 1U) ^ ((data[i] >> bit) & 1U);
            rem = (uint16_t)((rem << 1) & 0x7FFFU);
            if (in)
            {
                rem ^= 0x4599U;
            }
        }
    }

    return (uint16_t)(rem << 1); /* the 15-bit result goes out with a 0 appended */
}

/* Clocks out a 2-byte command plus its PEC. CS must already be low. */
static void send_cmd(uint16_t cmd)
{
    uint8_t frame[4] = { (uint8_t)(cmd >> 8), (uint8_t)cmd };
    uint16_t pec = isospi_pec15(frame, 2);
    frame[2] = (uint8_t)(pec >> 8);
    frame[3] = (uint8_t)pec;

    for (int i = 0; i < 4; i++)
    {
        spi_xfer(frame[i]);
    }
}

void isospi_send_cmd(uint16_t cmd)
{
    cs_low();
    send_cmd(cmd);
    cs_high();
}

void isospi_write_group(uint16_t cmd, uint8_t data[][6], int device_count)
{
    cs_low();
    send_cmd(cmd);

    /* Write data shifts THROUGH the chain like a shift register, so the
     * first group sent ends up in the chip FURTHEST from the LTC6820.
     * Send in reverse so data[0] lands in the first chip -- matching the
     * order reads come back in. Easy to get backwards; with one board on
     * the bench you can't tell, with nine you can. */
    for (int dev = device_count - 1; dev >= 0; dev--)
    {
        for (int i = 0; i < 6; i++)
        {
            spi_xfer(data[dev][i]);
        }
        uint16_t pec = isospi_pec15(data[dev], 6);
        spi_xfer((uint8_t)(pec >> 8));
        spi_xfer((uint8_t)pec);
    }
    cs_high();
}

int isospi_read_group(uint16_t cmd, uint8_t data[][6], int device_count)
{
    int failures = 0;

    cs_low();
    send_cmd(cmd);

    /* Reads come back nearest chip first: 6 data bytes then 2 PEC bytes
     * per chip. The PEC is checked per chip, so one bad board can be told
     * apart from a dead link. */
    for (int dev = 0; dev < device_count; dev++)
    {
        for (int i = 0; i < 6; i++)
        {
            data[dev][i] = spi_xfer(0xFF);
        }
        uint8_t pec_hi = spi_xfer(0xFF);
        uint8_t pec_lo = spi_xfer(0xFF);

        uint16_t received = (uint16_t)((pec_hi << 8) | pec_lo);
        if (received != isospi_pec15(data[dev], 6))
        {
            failures++;
        }
    }
    cs_high();

    return failures;
}
