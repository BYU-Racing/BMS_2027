#ifndef ISOSPI_TEST_H
#define ISOSPI_TEST_H

/*
 * isospi_test.h -- the first bring-up test: is the isoSPI link alive?
 *
 * FreeRTOS task that once a second writes a test pattern into config
 * register group A of every chip, reads it back, and prints PASS/FAIL per
 * chip, plus the raw bytes sent on MOSI and received on MISO. Alternates
 * between two patterns so a stale value can't pass. Run this first on any
 * new board before trusting the measurement tests.
 * Pass to xTaskCreate() with at least 512 words of stack (uses printf).
 */
void isospi_test_task(void *argument);

#endif /* ISOSPI_TEST_H */
