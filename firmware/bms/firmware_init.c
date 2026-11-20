#include "firmware.h"

void firmware_init(void) {
    MX_GPIO_Init();
    MX_FDCAN2_Init();
    MX_QUADSPI1_Init();
    MX_SPI2_Init();
    MX_UART5_Init();
    MX_IWDG_Init();
    MX_FDCAN3_Init();
}