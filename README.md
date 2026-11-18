# Blink-only firmware

This branch builds a standalone STM32G474 blinky: PA0 (LED1) is on for
500 ms, then off for 500 ms. It runs from the internal 16 MHz HSI and uses
a polled SysTick counter for delays, without interrupts. Only PA0 is configured as an output.

The build includes only startup, the blink loop and the required STM32 HAL
drivers. BMS tests, isoSPI, FreeRTOS, USB, CAN and the watchdog are excluded.
The previous BMS sources remain in the repository for reference.

Build with STM32CubeCLT tools on PATH:

```powershell
cmake --preset Release
cmake --build --preset Release
```

Outputs: `build/Release/BMSRework.elf`, `BMSRework.hex`, and `BMSRework.bin`.
Flash starts at `0x08000000`.

Flash with ST-Link:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Release/BMSRework.hex -v -rst
```

Debug builds use the same blink-only source selection:

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

The existing `.ioc` and `cmake/stm32cubemx` files describe the previous BMS
configuration and are not used by this build. Regenerating from CubeMX can
restore that configuration, so review generated changes before building.

USB flashing uses the STM32 factory ROM DFU bootloader, independently of this
application. With the board configured for pin-selected system-memory boot,
hold PB8/BOOT0 high and reset, connect USB, then select USB in CubeProgrammer.
Download the HEX with verification (BIN address: 0x08000000). Set BOOT0 low
and reset or power-cycle to run the blink loop. The USB connector must be
wired to PA11/PA12 and VDDUSB must be powered. Boot option bytes must permit
this boot mode; see ST AN2606 for STM32G47xxx/48xxx, Pattern 14.
