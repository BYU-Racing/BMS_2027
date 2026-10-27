/*
 * swo.c -- printf over the SWO pin (the default debug output). See swo.h.
 *
 * HOW IT WORKS: printf -> _write() (Src/System/syscall.c) -> __io_putchar()
 * here -> the Cortex-M4's ITM "stimulus port 0" -> the chip serializes it
 * out the SWO pin (PB3) -> the ST-LINK receives it -> STM32CubeProgrammer
 * prints it (`./bms.sh monitor`). The CPU only drops a byte into a small
 * hardware FIFO, so unlike semihosting it never stops the program.
 *
 * The probe, not this code, configures the trace hardware (baud rate,
 * enables) when it starts receiving. The SWO baud is derived from the CPU
 * clock, which is why bms.sh passes the clock (16 MHz) to the probe -- if
 * the CPU clock ever changes, update SWO_CPU_MHZ in bms.sh or the output
 * turns to garbage.
 *
 * SWO is OUTPUT ONLY. To send commands TO the board, use a UART (the
 * STLINK-V3 has a virtual COM port) -- note the Tag-Connect doesn't carry
 * UART.
 */

#include <stdint.h>

#include "include/swo.h"

/* Cortex-M4 debug/trace registers (ARMv7-M Architecture Reference Manual). */
#define DEMCR            (*(volatile uint32_t *)0xE000EDFCUL)
#define DEMCR_TRCENA     (1UL << 24)

#define ITM_STIM0_8      (*(volatile uint8_t  *)0xE0000000UL) /* byte write = one 8-bit packet */
#define ITM_STIM0        (*(volatile uint32_t *)0xE0000000UL) /* read: bit 0 = FIFO ready */
#define ITM_TER          (*(volatile uint32_t *)0xE0000E00UL)
#define ITM_TCR          (*(volatile uint32_t *)0xE0000E80UL)
#define ITM_TCR_ITMENA   (1UL << 0)

int __io_putchar(int ch)
{
    /* Only send once the probe has switched tracing on. Without a probe the
     * stimulus port never reports ready and this would spin forever -- so
     * running the board standalone must silently drop output instead. */
    if (!(DEMCR & DEMCR_TRCENA) || !(ITM_TCR & ITM_TCR_ITMENA) || !(ITM_TER & 1UL))
    {
        return ch;
    }

    while (!(ITM_STIM0 & 1UL)) {}
    ITM_STIM0_8 = (uint8_t)ch;

    return ch;
}
