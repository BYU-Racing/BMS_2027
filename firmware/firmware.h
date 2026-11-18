// Filename: firmware.h
// Desc: Firmware entry point for all BMS code.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Configures peripherals. Called after kernel initialization, before scheduler starts.
void firmware_init(void);

#ifdef __cplusplus
}
#endif