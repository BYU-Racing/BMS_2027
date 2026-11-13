#ifndef BOARD_CHECK_H
#define BOARD_CHECK_H

/*
 * board_check.h -- the everyday slave board verification test.
 *
 * FreeRTOS task that wakes the chain, reads all 12 cells and every
 * thermistor (26 Hz filtered mode, for accuracy), prints one compact table
 * per slave board -- each cell's voltage with the same-numbered thermistor
 * beside it, the stack total, then the leftover thermistors -- and then
 * goes silent so every board sleeps before the next check (~11 s apart).
 *
 * Example output (one slave, 10k test resistors on TH6 and TH8):
 *   === Board check 1 ===
 *    Slave 0
 *     C0  3.4011 V   TH0  NOT CONNECTED
 *     C6  3.4028 V   TH6  24.3 C
 *     ...
 *     Stack 40.979 V
 *     TH12 NOT CONNECTED   TH13 NOT CONNECTED   ...
 *
 * Pass to xTaskCreate() with at least 512 words of stack (uses printf).
 */
void board_check_task(void *argument);

#endif /* BOARD_CHECK_H */
