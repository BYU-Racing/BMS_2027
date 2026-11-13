# Imported bench firmware

Source: `Downloads/BMS_working_test_code/BMS_code_V2` from this workstation.
The measurement algorithms, thermistor coefficients/mapping, SPI pins, PEC,
test modes and one-slave default are preserved here. No Downloads files are
needed to build this repository.

Integration changes:

- `gpio.c/h` are named `bench_gpio.c/h` to coexist with CubeMX's HAL GPIO files.
- `bench_app.c` selects one test and creates an independent PA0 heartbeat task.
  PA1 replaces PA0 as the measurement activity indicator; PA15 remains fault.
- This repository provides startup, syscalls, HAL clock/timebase and FreeRTOS.
  The reference startup, SystemInit, kernel and syscall files are not imported.
- CPU faults route through the existing interrupt handlers to `fault_halt`.
  Stack overflow, allocation failure and RTOS assertions use the same handler.
- Report printing no longer suspends scheduling, allowing the heartbeat to run.
  SWO writes have a bounded wait so a stalled receiver cannot hang the test.
- The clock is kept at the reference's 16 MHz; SPI2 is mode 0 at 500 kHz.

This preserves the reference's bench-test scope: measurements and diagnostics,
with balancing disabled. It does not implement production pack protection.
See the root README for build, mode selection, monitoring and flash commands.
