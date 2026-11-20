#pragma once
#include <stdbool.h>
#include <stdint.h>

/* FlexCAN_T4.h includes a ton of things we don't have available in a testing environment,
 * so we can't use FlexCAN_T4.h when testing. Unfortunately, `CAN_message_t` comes from
 * FlexCAN_T4.h, so we have to make a "fake" `CAN_message_t` so that we can run the test.
 * This is literally just copied from FlexCAN_T4.h. */
typedef struct CAN_message_t {
    uint32_t id; // can identifier
    struct {
        bool extended; // identifier is extended (29-bit)
        bool remote;   // remote transmission request packet type
    } flags;
    uint8_t len;    // length of data
    uint8_t buf[8]; // data
} CAN_message_t;