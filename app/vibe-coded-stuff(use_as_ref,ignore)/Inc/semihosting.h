#ifndef SEMIHOSTING_H
#define SEMIHOSTING_H

/*
 * semihosting.h -- printf through the debugger, the fallback when SWO
 * isn't working.
 *
 * Defines __io_putchar (same hook as swo.c -- only one of the two is
 * compiled in). To use it:
 *   wsl bash -c "CMAKE_ARGS=-DBMS_PRINTF_SWO=OFF ./bms.sh configure build flash"
 *   wsl ./bms.sh semihost
 * and to switch back:
 *   wsl bash -c "CMAKE_ARGS=-DBMS_PRINTF_SWO=ON ./bms.sh configure build flash"
 *
 * Checks for an attached debugger first, so a semihosting build still runs
 * standalone (output is just dropped). Slow: the CPU halts per character.
 */
int __io_putchar(int ch);

#endif /* SEMIHOSTING_H */
