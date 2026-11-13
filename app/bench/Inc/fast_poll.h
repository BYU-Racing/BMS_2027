#ifndef FAST_POLL_H
#define FAST_POLL_H

/*
 * fast_poll.h -- polling-speed benchmark.
 *
 * FreeRTOS task that reads every cell and thermistor back to back as fast
 * as the link allows (chips kept awake, microsecond busy-waits), and once
 * a second prints the achieved scan rate, time per scan, PEC/stale error
 * counts, cell min/max/average/stack and every thermistor temperature.
 * Mode and strategy knobs, plus the numbers measured on the bench, are at
 * the top of Src/fast_poll.c.
 * Pass to xTaskCreate() with at least 512 words of stack (uses printf).
 */
void fast_poll_task(void *argument);

#endif /* FAST_POLL_H */
