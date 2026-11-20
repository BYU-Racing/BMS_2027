#include "can_buses.h"
#include "can_serde.h"
#include "../shared/util.h"

extern FDCAN_HandleTypeDef hfdcan2, hfdcan3;

CanBus DataCAN;
CanBus MotorCAN;

#ifdef __cplusplus
extern "C" {
#endif

void can_buses_init(void) {
    can_bus_init(&DataCAN, &hfdcan2);
    can_bus_init(&MotorCAN, &hfdcan3);
    if (!can_bus_start(&DataCAN)) {
        Error_Handler();
    }
    PRINTF("DataCAN initialized\n\n");

    if (!can_bus_start(&MotorCAN)) {
        Error_Handler();
    }
    PRINTF("MotorCAN initialized\n\n");
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
    // If the callback was called for a reason other than a new message, we don't care.
    if (!(RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE)) {
        return;
    }

    // Determine which bus the interrupt was for and call the appropriate handler.
    if (hfdcan == DataCAN.hfdcan) {
        can_bus_on_rx_interrupt(&DataCAN);
    } else if (hfdcan == MotorCAN.hfdcan) {
        can_bus_on_rx_interrupt(&MotorCAN);
    }
}

#ifdef __cplusplus
}
#endif