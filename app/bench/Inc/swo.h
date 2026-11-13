#ifndef SWO_H
#define SWO_H

/*
 * swo.h -- printf over SWO (PB3), the default debug output.
 *
 * Defines __io_putchar, which Src/System/syscall.c's _write() (and
 * therefore printf) calls once per character. View the output live with
 * `./bms.sh monitor`. Characters are silently dropped until a probe is
 * listening, so the board runs fine standalone.
 *
 * Bench note: SWO travels on its own Tag-Connect pin. When `monitor` shows
 * "Reception Started" and then nothing while the board is clearly running,
 * it has always been that pin losing contact -- reseat/clip the cable, or
 * fall back to semihosting (see semihosting.h).
 */
int __io_putchar(int ch);

#endif /* SWO_H */
