/*
 * semihosting.c -- printf through the debugger (the fallback debug output).
 * See semihosting.h.
 *
 * HOW IT WORKS: each character triggers a special breakpoint (BKPT 0xAB).
 * The attached debugger (`./bms.sh semihost` runs ST-LINK gdbserver + gdb)
 * catches it, reads the character out of the chip's memory, prints it, and
 * resumes the CPU. It needs only the SWD data/clock lines -- no SWO pin --
 * which is why it's the fallback when SWO is unreliable.
 *
 * DOWNSIDE: the CPU halts for every character, so printing is very slow
 * and disturbs timing. Fine for bench reports, wrong for anything timed.
 */

#include <stdint.h>

#include "include/semihosting.h"

#define SYS_WRITEC 0x03   /* semihosting operation "write one character" */

// #define SEMIHOSTING      /* semihosting serial monitor backup flag */

/* Debug Halting Control and Status Register. Bit 0 (C_DEBUGEN) is set once
 * a debug probe has taken control of the debug port, and reads 0 with no
 * debugger attached. */
#define DHCSR            (*(volatile uint32_t *)0xE000EDF0UL)
#define DHCSR_C_DEBUGEN  (1UL << 0)

#if SEMIHOSTING

int __io_putchar(int ch)
{
    /* With no debugger attached, a semihosting BKPT would hard-fault (or
     * hang) the chip -- so skip the call and drop the character instead. */
    if (!(DHCSR & DHCSR_C_DEBUGEN))
    {
        return ch;
    }

    char c = (char)ch;

    /* ARM semihosting call: r0 = operation number, r1 = pointer to the
     * argument (here, the character). BKPT 0xAB is the semihosting trap. */
    register int r0 __asm("r0") = SYS_WRITEC;
    register void *r1 __asm("r1") = &c;

    __asm volatile ("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");

    return ch;
}

#endif