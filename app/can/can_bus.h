#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "CAN_message_t.h"

#define CAN_RX_SIZE 256
#define CAN_TX_SIZE 16

typedef struct CanBus {
    FDCAN_HandleTypeDef *hfdcan;

    CAN_message_t rx_buf[CAN_RX_SIZE];
    CAN_message_t tx_buf[CAN_TX_SIZE];

    volatile uint32_t rx_head;   /* written by the ISR */
    volatile uint32_t rx_tail;   /* written by the main loop */
    volatile uint32_t tx_head;   /* main loop only */
    volatile uint32_t tx_tail;   /* main loop only */
    volatile uint32_t rx_overflow;
} CanBus;

#ifdef __cplusplus
extern "C" {
#endif 

void can_bus_init(CanBus *bus, FDCAN_HandleTypeDef *hfdcan);

bool can_bus_start(CanBus *bus);

bool can_bus_write(CanBus *bus, const CAN_message_t *msg);

bool can_bus_read(CanBus *bus, CAN_message_t *out);

void can_bus_service(CanBus *bus);

void can_bus_on_rx_interrupt(CanBus *bus);

#ifdef __cplusplus
}
#endif