/*
 * fault.c -- one place every "this should never happen" path ends up. See
 * fault.h.
 *
 * WHY: by default an STM32 that crashes just sits in an infinite loop with
 * no sign of life -- indistinguishable from "busy". Routing every fault to
 * one function that lights a dedicated LED makes a crash visible on the
 * bench, and a debugger attached afterwards finds the core parked here.
 * In the real BMS this is also where you'd force the system into a SAFE
 * state (open the AIRs / signal a BMS fault) -- a crashed BMS must never
 * look healthy.
 */

#include "hw.h"
#include "led.h"
#include "fault.h"

void fault_halt(void)
{
    /* Mask interrupts first: this stops the RTOS scheduler and every ISR,
     * so nothing else runs (or overwrites the evidence) from here on. */
    __asm volatile ("cpsid i" ::: "memory");

    led_set(HW_LED_HEARTBEAT, 0);
    led_set(HW_LED_ACTIVE, 0);
    led_set(HW_LED_FAULT, 1);

    for (;;) {}
}

