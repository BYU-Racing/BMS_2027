#include "can_bus.h"
#include <string.h>

void can_bus_init(CanBus *bus, FDCAN_HandleTypeDef *hfdcan) {
    bus->hfdcan = hfdcan;
    bus->rx_head = bus->rx_tail = 0;
    bus->tx_head = bus->tx_tail = 0;
    bus->rx_overflow = 0;
}

bool can_bus_start(CanBus *bus) {
    // Accepts unknown IDs and places them in RX FIFO 0
    if (HAL_FDCAN_ConfigGlobalFilter(
            bus->hfdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT_REMOTE,
            FDCAN_REJECT_REMOTE
        ) != HAL_OK) {
        return false;
    }

    // Starts the CAN peripheral
    if (HAL_FDCAN_Start(bus->hfdcan) != HAL_OK) {
        return false;
    }

    // Enables the interrupt for new messages
    if (HAL_FDCAN_ActivateNotification(bus->hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
        return false;
    }

    return true;
}

bool can_bus_write(CanBus *bus, const CAN_message_t *msg) {
    uint32_t next_head = (bus->tx_head + 1) & (CAN_TX_SIZE - 1);

    // Checks if TX buffer is full.
    if (next_head == bus->tx_tail) {
        return false;
    }

    bus->tx_buf[bus->tx_head] = *msg;
    bus->tx_head = next_head;

    // Tries to send messages right away.
    can_bus_service(bus);

    return true;
}

bool can_bus_read(CanBus *bus, CAN_message_t *out) {
    // Checks if RX buffer is empty.
    if (bus->rx_head == bus->rx_tail) {
        return false;
    }

    // Reads the next message from the buffer and copies it to out.
    *out = bus->rx_buf[bus->rx_tail];

    // Moves tail pointer to next message.
    bus->rx_tail = (bus->rx_tail + 1) & (CAN_RX_SIZE - 1);

    return true;
}

void can_bus_service(CanBus *bus) {
    // Checks if any messages are waiting to be sent and if the CAN peripheral has space in its TX
    // FIFO buffer
    while (bus->tx_tail != bus->tx_head && HAL_FDCAN_GetTxFifoFreeLevel(bus->hfdcan) > 0) {
        // Gets the next message to send
        const CAN_message_t *message = &bus->tx_buf[bus->tx_tail];

        // Configures the FDCAN header for the message
        FDCAN_TxHeaderTypeDef header = {0};
        header.Identifier = message->id;
        header.IdType = message->flags.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        header.TxFrameType = message->flags.remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
        header.DataLength = ((uint32_t)message->len); /* FDCAN_DLC_BYTES_n */
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0;

        // Adds message to the hardware TX FIFO queue. If fails, breaks the loop and tries again
        // later.
        if (HAL_FDCAN_AddMessageToTxFifoQ(bus->hfdcan, &header, message->buf) != HAL_OK) {
            break;
        }

        // Advances the tail pointer to the next message in the software TX buffer
        bus->tx_tail = (bus->tx_tail + 1) & (CAN_TX_SIZE - 1);
    }
}

void can_bus_on_rx_interrupt(CanBus *bus) {
    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];

    while (HAL_FDCAN_GetRxFifoFillLevel(bus->hfdcan, FDCAN_RX_FIFO0) > 0) {
        // If errors occur, breaks.
        if (HAL_FDCAN_GetRxMessage(bus->hfdcan, FDCAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK) {
            break;
        }

        uint32_t next_head = (bus->rx_head + 1) & (CAN_RX_SIZE - 1);

        // If RX buffer is full, increment overflow counter and discard the message.
        if (next_head == bus->rx_tail) {
            bus->rx_overflow++;
            continue;
        }

        uint32_t dlc = rx_header.DataLength;
        CAN_message_t *message = &bus->rx_buf[bus->rx_head];
        memset(message, 0, sizeof(*message)); /* clears buffer */
        message->id = rx_header.Identifier;
        message->len = (dlc > 8) ? 8 : (uint8_t)dlc;
        message->flags.extended = (rx_header.IdType == FDCAN_EXTENDED_ID);
        message->flags.remote = (rx_header.RxFrameType == FDCAN_REMOTE_FRAME);
        for (uint8_t i = 0; i < message->len; i++)
            message->buf[i] = rx_data[i];

        // Advances the head pointer to the next position in the RX buffer
        bus->rx_head = next_head;
    }
}