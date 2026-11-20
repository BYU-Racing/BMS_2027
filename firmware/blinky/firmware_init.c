#include "firmware.h"

void firmware_init(void) {
    // initialize peripherals and create RTOS tasks.
    MX_GPIO_Init();
}