#ifndef DBG_PRINT_H
#define DBG_PRINT_H

#ifdef NUCLEO_DEBUG
void dbg_init(void);
void dbg_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
/* Custom board build: compiles to nothing */
#define dbg_init() ((void)0)
#define dbg_printf(...) ((void)0)
#endif

#endif