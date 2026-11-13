#ifndef FAULT_H
#define FAULT_H

/*
 * fault.h -- stop everything and show it.
 *
 * fault_halt() masks interrupts (so the scheduler stops too), turns the
 * heartbeat and activity LEDs off and the fault LED (LED7) on, and spins forever.
 * Called from: the CPU fault handlers, FreeRTOS configASSERT, the FreeRTOS
 * stack-overflow and out-of-heap hooks, and main() if startup fails.
 * Needs led_init() to have run -- main() does that first thing.
 */
void fault_halt(void) __attribute__((noreturn));

#endif /* FAULT_H */
