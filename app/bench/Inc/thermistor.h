#ifndef THERMISTOR_H
#define THERMISTOR_H

#include <stdint.h>

/*
 * thermistor.h -- turning a thermistor divider voltage into degrees C.
 *
 * Two steps, kept separate on purpose:
 *   1. divider voltage -> thermistor resistance  (depends on the BOARD:
 *      pull-up value and what it's tied to -- see hw.h)
 *   2. resistance -> temperature                 (depends on the PART:
 *      the thermistor's R/T curve -- see thermistor.c)
 * When readings were wrong during bring-up, it was always step 1 (a wrong
 * pull-up value, and a VREF1/VREF2 short on the board), never the curve.
 * Debug them separately: print the resistance, compare it against the
 * resistor you put in, and only then look at temperature.
 */

/* Step 2: resistance (ohms) -> degrees C using the Vishay NTCS0603E3103FLT
 * R/T curve. Returns 1 and writes *temp_c on success, or 0 if the
 * resistance is outside the curve's -55..150 C range. */
int thermistor_temp_c(float r_ohms, float *temp_c);

typedef enum
{
    THERM_OK,          /* *r_ohms and *temp_c valid */
    THERM_MISSING,     /* within 5% of VREF2: nothing pulling the node down (open / not connected) */
    THERM_SHORT,       /* within 1% of GND: thermistor or wiring shorted */
    THERM_OFF_CURVE,   /* *r_ohms valid, but outside -55..150 C */
    THERM_NO_DATA,     /* a register still held 0xFFFF (no conversion result) */
} therm_status_t;

/* Steps 1 + 2 together. v_code is the mux output (GPIO1/GPIO2) and
 * ref_code is VREF2, both raw LTC6811 ADC codes (100 uV per count) from
 * the SAME conversion. Using the ratio of the two means any ADC gain error
 * cancels out. Uses HW_THERM_PULLUP_OHMS from hw.h. Fills *r_ohms (THERM_OK
 * / THERM_OFF_CURVE) and *temp_c (THERM_OK only). */
therm_status_t thermistor_from_divider(uint16_t v_code, uint16_t ref_code,
                                       float *r_ohms, float *temp_c);

#endif /* THERMISTOR_H */
