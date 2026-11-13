/*
 * led.c -- master board status LEDs. See led.h.
 */

#include "bench_gpio.h"
#include "hw.h"
#include "led.h"

/* LED index -> pin. A table lets callers use a simple 0-6 index while the
 * actual (non-contiguous) pins stay in hw.h. */
static const uint8_t led_pins[HW_LED_COUNT] = {
    HW_LED0_PIN, HW_LED1_PIN, HW_LED2_PIN, HW_LED3_PIN,
    HW_LED4_PIN, HW_LED5_PIN, HW_LED6_PIN,
};

void led_init(void)
{
    gpio_clock_enable(HW_LED_PORT);

    /* Set each output level to "off" BEFORE switching the pin to output,
     * so no LED blips on during startup. Same trick as the isoSPI CS pin. */
    for (int i = 0; i < HW_LED_COUNT; i++)
    {
        gpio_write(HW_LED_PORT, led_pins[i], 0);
        gpio_set_mode(HW_LED_PORT, led_pins[i], GPIO_MODE_OUTPUT);
    }
}

void led_set(int index, int on)
{
    gpio_write(HW_LED_PORT, led_pins[index], on);
}
