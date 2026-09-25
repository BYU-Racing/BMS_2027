#pragma once

#include <stdint.h>
// This will store data to be logged
typedef struct {
    // Voltages
    // Temperatures
    // Control Status
    // What else?
} DataLoggingInputs;

// To-do
// 1. Study CAN message structure and how to send messages

// CAN messages are in this form: 
// 1. The ID: 4 byte (32-bits)
// 2. The message: A pointer to an array (Likely 8 bytes (64-bits)); the message itself)
// 3. The length: (1-8) of the data in the array.
// The function returns 1 if the message was sent successfully, or 0 if there was an error
uint8_t can_send_message(uint32_t id, uint8_t *data, uint8_t length);

// CAN data structure:
// 1. Start of Frame (SOF) - Single dominant bit marking the start
// 2. Arbitration Field - Contains message identifier (11-bit or 29-bit) and RTR bit
// 3. Control Field - Contains IDE, reserved bit, and Data Length Code (DLC)
// 4. Data Field - 0 to 8 bytes of data
// 5. CRC Field - 15-bit cyclic redundancy check for error detection
// 6. ACK Field - Acknowledgment from receiving nodes
// 7. End of Frame (EOF) - 7 recessive bits

// Example usage:
// uint8_t data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
// uint32_t id = 0x123; // Message ID
// uint8_t length = 8;  // Data length
// can_send_message(id, data, length);
