#ifndef BENCH_GPIO_H
#define BENCH_GPIO_H

#include <stdint.h>

/*
 * gpio.h -- a minimal bare-metal GPIO layer for the STM32G474 (from the
 * starter code). Pins are named as (port base address, pin number 0-15),
 * e.g. gpio_write(GPIOA_BASE, 0, 1).
 *
 * WHY NOT THE ST HAL: this project talks to registers directly so every
 * line maps to the reference manual (RM0440) and nothing is hidden. That's
 * great for learning and bring-up. For your rewrite, the ST HAL/LL drivers
 * are a perfectly good choice too -- just keep pin numbers in hw.h either
 * way.
 */

/* GPIO port base addresses (RM0440 memory map). */
#define GPIOA_BASE 0x48000000UL
#define GPIOB_BASE 0x48000400UL
#define GPIOC_BASE 0x48000800UL
#define GPIOD_BASE 0x48000C00UL
#define GPIOE_BASE 0x48001000UL
#define GPIOF_BASE 0x48001400UL
#define GPIOG_BASE 0x48001800UL

/* The two MODER bits per pin. */
typedef enum
{
    GPIO_MODE_INPUT  = 0,
    GPIO_MODE_OUTPUT = 1,
    GPIO_MODE_AF     = 2,   /* pin is driven by a peripheral (SPI, UART, ...) */
    GPIO_MODE_ANALOG = 3,
} gpio_mode_t;

/* Turns on the peripheral clock for a GPIO port. A port's registers ignore
 * writes until its clock is on -- the classic "my pin does nothing" bug.
 * Call once before using any pin on that port (calling again is harmless). */
void gpio_clock_enable(uint32_t port_base);

/* Sets a single pin's mode (input / output / alternate-function / analog). */
void gpio_set_mode(uint32_t port_base, uint8_t pin, gpio_mode_t mode);

/* Selects which peripheral function (AF0-AF15) a pin uses. Only takes effect
 * once the pin's mode is GPIO_MODE_AF. The AF number for each pin/peripheral
 * pair is in the datasheet's "alternate function" table. */
void gpio_set_af(uint32_t port_base, uint8_t pin, uint8_t af);

/* Drives an output pin high (value != 0) or low (value == 0). */
void gpio_write(uint32_t port_base, uint8_t pin, int value);

/* Flips an output pin's current state. */
void gpio_toggle(uint32_t port_base, uint8_t pin);

#endif /* BENCH_GPIO_H */
