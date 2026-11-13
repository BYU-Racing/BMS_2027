#ifndef LED_H
#define LED_H

/*
 * led.h -- the master board's 7 status LEDs (pins in hw.h).
 * What each LED means is defined in hw.h (HW_LED_ACTIVE, HW_LED_FAULT).
 */

/* Configures all 7 LED pins as outputs, all off. main() calls this before
 * anything else so the fault LED works from the very start (a fault during
 * startup can still be seen). */
void led_init(void);

/* Turns LED `index` (0-6) on (nonzero) or off (0). Index 0 = board LED1. */
void led_set(int index, int on);

#endif /* LED_H */
