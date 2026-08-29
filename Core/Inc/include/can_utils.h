#pragma once

enum MessageId: uint32_t {
    /* CAN msg ids */
    ControlCommand = 192,
};

uint8_t can_recieve_msg(uint32_t *id, uint8_t *data, uint8_t *length);