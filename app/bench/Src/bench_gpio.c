/*
 * gpio.c -- register-level GPIO for the STM32G474. See gpio.h.
 * Register offsets and bit layouts are from RM0440, "GPIO registers".
 */

#include "bench_gpio.h"

/* Peripheral clock enable register for the GPIO ports (RCC_AHB2ENR). */
#define RCC_AHB2ENR (*(volatile uint32_t *)0x4002104CUL)

/* Per-port registers, as offsets from the port's base address.
 * `volatile` tells the compiler every access really touches hardware, so it
 * must not cache or skip them -- required for all memory-mapped registers. */
#define MODER_REG(port_base) (*(volatile uint32_t *)((port_base) + 0x00UL))
#define ODR_REG(port_base)   (*(volatile uint32_t *)((port_base) + 0x14UL))
#define BSRR_REG(port_base)  (*(volatile uint32_t *)((port_base) + 0x18UL))

void gpio_clock_enable(uint32_t port_base)
{
    /* RCC_AHB2ENR bit N enables port N (A=0, B=1, C=2, ...); ports are
     * spaced 0x400 apart in the address map, so this recovers N from the
     * base address instead of needing a separate lookup table. */
    uint32_t bit = (port_base - GPIOA_BASE) / 0x400UL;
    RCC_AHB2ENR |= (1UL << bit);
}

void gpio_set_mode(uint32_t port_base, uint8_t pin, gpio_mode_t mode)
{
    /* Two bits per pin: clear this pin's pair, then set the new mode
     * (read-modify-write so the other 15 pins are untouched). */
    uint32_t shift = (uint32_t)pin * 2U;
    MODER_REG(port_base) = (MODER_REG(port_base) & ~(3UL << shift)) | ((uint32_t)mode << shift);
}

void gpio_set_af(uint32_t port_base, uint8_t pin, uint8_t af)
{
    /* AFRL (0x20) holds pins 0-7, AFRH (0x24) pins 8-15, 4 bits per pin. */
    volatile uint32_t *afr = (volatile uint32_t *)(port_base + 0x20UL + (pin >= 8 ? 4UL : 0UL));
    uint32_t shift = (uint32_t)(pin & 7U) * 4U;
    *afr = (*afr & ~(0xFUL << shift)) | ((uint32_t)af << shift);
}

void gpio_write(uint32_t port_base, uint8_t pin, int value)
{
    /* BSRR: writing bit N sets pin N, writing bit N+16 clears it. It's a
     * single write with no read-modify-write, so it can't clobber another
     * pin even if an interrupt changes the port in between -- prefer it
     * over ODR |= / &= for outputs. */
    BSRR_REG(port_base) = value ? (1UL << pin) : (1UL << (pin + 16));
}

void gpio_toggle(uint32_t port_base, uint8_t pin)
{
    int currently_high = (ODR_REG(port_base) & (1UL << pin)) != 0;
    gpio_write(port_base, pin, !currently_high);
}
