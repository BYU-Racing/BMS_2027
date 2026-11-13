#ifndef CELL_TEST_H
#define CELL_TEST_H

/*
 * cell_test.h -- the detailed debug report.
 *
 * FreeRTOS task that repeatedly: wakes the chain, reports whether each chip
 * really slept since last time, measures all 12 cells plus the status
 * values (stack, die temperature, VA/VD supplies), measures every
 * thermistor with its raw millivolts and computed resistance, flags any
 * mux channel that didn't switch, prints it all, then goes quiet for 6 s so
 * the chips sleep. Knobs (filtered/normal mode, cells on/off, a multimeter
 * "hold" mode) are at the top of Src/cell_test.c.
 *
 * For a quick pass/fail look at a board, use board_check instead; come here
 * when something in that output looks wrong.
 * Pass to xTaskCreate() with at least 512 words of stack (uses printf).
 */
void cell_test_task(void *argument);

#endif /* CELL_TEST_H */
