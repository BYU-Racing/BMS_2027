## BMS bench tests

This branch includes the LTC6820/LTC6811 bring-up tests from
`Downloads/BMS_working_test_code/BMS_code_V2`, integrated with this repository's
HAL startup and FreeRTOS kernel. By default it measures one slave board's
12 cells and 16 thermistors, reports cell voltages, stack voltage and temperatures
over SWO, then lets the slave sleep for six seconds between checks.

PA0 (LED1) blinks once per second: 500 ms on, 500 ms off, independently of
measurements. PA1 (LED2) indicates measurement activity. PA15 (LED7) indicates
communication errors, and stays on after CPU/RTOS faults; the heartbeat stops
on a fatal fault. This is bench firmware, with balancing off and no pack
protection/control task.

The CPU and PCLK1 run from the internal 16 MHz HSI, preserving the reference
driver's timing and 500 kHz SPI2 clock. SPI2 uses PF1 (SCK), PB14 (MISO), PB15
(MOSI), and PB6 (software CS). The existing generated peripheral init functions
and `app_freertos.c` are not used; no CAN, logging, watchdog, or USB CDC is started.
Board-specific values and the slave count live in
[app/bench/Inc/hw.h](app/bench/Inc/hw.h).

Build with the STM32CubeCLT tools on PATH:

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

Outputs are `build/Debug/BMSRework.elf`, `BMSRework.hex`, and `BMSRework.bin`.
The application starts at flash address `0x08000000`.

Choose another bench test by configuring and rebuilding:

```powershell
cmake --preset Debug -DBMS_TEST_TASK=2
cmake --build --preset Debug
```

| BMS_TEST_TASK | Function |
|---|---|
| 0 | Detailed cell/thermistor diagnostics, wake/sleep and mux checks |
| 1 | Fast polling benchmark |
| 2 | isoSPI config write/read link test |
| 3 | Compact board check (default) |

The selection persists in the build cache; use `-DBMS_TEST_TASK=3` to restore
the default. Only one test owns the isoSPI link at a time.

Read SWO through an ST-Link with PB3/SWO connected (CPU frequency is 16 MHz):

```powershell
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -startswv freq=16 portnumber=0
```

Output is dropped when tracing is disabled; standalone measurements and the
heartbeat continue without a probe. Benchmark results include the heartbeat's
small scheduling overhead.

Flash using either interface:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/BMSRework.hex -v -rst
# With the MCU already in USB DFU mode:
STM32_Programmer_CLI -c port=USB1 -w build/Debug/BMSRework.bin 0x08000000 -v
```

After USB flashing, set BOOT0 low and reset.

### Upload with ST-Link / SWD

Power the target and connect ST-Link SWDIO to PA13, SWCLK to PA14, GND to GND,
and its target voltage reference to the board's 3.3 V rail. Connect NRST if
available. In STM32CubeProgrammer, select ST-LINK / SWD, connect, open
`build/Debug/BMSRework.hex`, download with verification, and reset to run.
Use connect-under-reset if normal connection fails and NRST is connected.

### Upload with the MCU's USB DFU bootloader

Use the board's native USB connection to PA11 (D-) / PA12 (D+), with VDDUSB
powered at 3.3 V. Enter system-memory boot mode before connecting through
STM32CubeProgrammer's USB interface. For hardware BOOT0 selection, the required
option bits are `nSWBOOT0=1`, `nBOOT1=1`, and `BOOT_LOCK=0`: hold PB8/BOOT0 high
and reset. Actual option-byte values must be checked on the device; a first
ST-Link connection may be needed to enable pin-based boot selection.

PB8/BOOT0 was used for CAN RX in the original board configuration; the current
configuration reserves it as a boot input. Check the board wiring before
driving that pin high, including whether a CAN transceiver drives it.

In STM32CubeProgrammer, select USB, refresh and connect to the DFU device,
open `build/Debug/BMSRework.hex`, then download with verification. Return BOOT0
low and reset to run the bench application. When using the `.bin` instead,
specify download address `0x08000000`.

USB uploading uses the factory ROM bootloader; the running bench application
does not enumerate as a USB device. Hardware and boot-mode requirements are
documented in ST's [AN2606, STM32G47xxx/48xxx section and Pattern 14](https://www.st.com/resource/en/application_note/an2606-stm32-microcontroller-system-memory-boot-mode-stmicroelectronics.pdf).

The `.ioc` describes the generated BMS peripherals rather than the imported
register-level bench setup. After CubeMX regeneration, recheck the clock,
startup calls, RTOS diagnostics, fault handlers and the CMake source selection.

#### VS code STM32CubeMX extension
-  Name: STM32CubeIDE for Visual Studio Code
-  Id: STMicroelectronics.stm32-vscode-extension
-  Description: STM32 embedded development support added to Visual Studio Code.
-  Version: 3.10.0
-  Publisher: STMicroelectronics
-  VS Marketplace Link: https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension
#### Documentation
Documentation can be found inside the "Docs" folder
