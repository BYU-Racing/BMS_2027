/*
 * thermistor.c -- divider voltage -> resistance -> temperature. See
 * thermistor.h for why those are two separate steps.
 */

#include <math.h>

#include "hw.h"
#include "thermistor.h"

/* RUGBY INPUT: thermistor part Vishay NTCS0603E3103FLT -- 10 kohm +/-1% at
 * 25 C, B25/85 = 3435 K. The coefficients below are copied from the CSV
 * Rugby exported from Vishay's "NTC RT Calculator" for that part. That tool
 * gives a 4-term fit (a Steinhart-Hart style curve):
 *
 *   T [K] = 1 / (A1 + B1*x + C1*x^2 + D1*x^3),   x = ln(R / R25)
 *
 * with one coefficient set below 25 C (R > R25) and one at/above 25 C.
 * It matches Vishay's table to well under 0.1 C -- the part's own +/-1%
 * tolerance (~0.3 C at 25 C) is the bigger error.
 *
 * WHY NOT THE SIMPLE "B-VALUE" FORMULA: 1/T = 1/T25 + ln(R/R25)/B is only
 * exact at 25 C and 85 C (that's what "B25/85" means) and drifts a degree
 * or more elsewhere. The 4-term fit is just as cheap to compute.
 *
 * Checkpoints from the table, handy for testing your own version:
 *   10 000 ohm -> 25.0 C, 4 499 ohm -> 48 C, 22 391 ohm -> 4 C.
 * If you change thermistor part, regenerate these from the vendor's tool. */
#define R25_OHMS        10000.0f
#define R_AT_MINUS_55C  440820.15f  /* coldest table entry */
#define R_AT_150C       301.43f     /* hottest table entry */

/* { A1, B1, C1, D1 } -- RUGBY INPUT (Vishay CSV). */
static const float coeff_below_25c[4] = {
    3.354016434680420e-3f, 3.13649052939535e-4f, 1.78017282463078e-6f, 3.07414016216838e-7f,
};
static const float coeff_from_25c[4] = {
    3.354016434680210e-3f, 3.07403768384794e-4f, 1.0191527970039e-5f, 9.09371179496435e-7f,
};

int thermistor_temp_c(float r_ohms, float *temp_c)
{
    /* Don't extrapolate past the table -- an out-of-range resistance means
     * a fault, not a real temperature. (Written as !(in range) so a NaN
     * also counts as out of range.) */
    if (!(r_ohms <= R_AT_MINUS_55C && r_ohms >= R_AT_150C))
    {
        return 0;
    }

    /* NTC: resistance goes UP as it gets colder, so R > R25 means < 25 C. */
    const float *k = r_ohms > R25_OHMS ? coeff_below_25c : coeff_from_25c;
    float x = logf(r_ohms / R25_OHMS);

    /* Horner's form of A1 + B1*x + C1*x^2 + D1*x^3. */
    *temp_c = 1.0f / (k[0] + x * (k[1] + x * (k[2] + x * k[3]))) - 273.15f;
    return 1;
}

therm_status_t thermistor_from_divider(uint16_t v_code, uint16_t ref_code,
                                       float *r_ohms, float *temp_c)
{
    /* 0xFFFF is what a result register holds after a clear / before any
     * conversion has written it. */
    if (v_code == 0xFFFFU || ref_code == 0xFFFFU)
    {
        return THERM_NO_DATA;
    }

    /* Open: with nothing below the pull-up, the node floats up to VREF2.
     * Anything within 5% of it is > ~20x the pull-up resistance -- colder
     * than any real pack -- so treat it as not connected. */
    if (v_code >= ref_code - ref_code / 20U)
    {
        return THERM_MISSING;
    }

    /* Short: the node is pulled (almost) to ground. */
    if (v_code <= ref_code / 100U)
    {
        return THERM_SHORT;
    }

    /* Divider: V = VREF2 * R / (R + R_pullup), solved for R:
     *   R = R_pullup * V / (VREF2 - V)
     * The raw codes can be used directly -- the 100 uV/count scale cancels
     * in the ratio, and so does any ADC gain error. */
    *r_ohms = HW_THERM_PULLUP_OHMS * (float)v_code / (float)(ref_code - v_code);
    return thermistor_temp_c(*r_ohms, temp_c) ? THERM_OK : THERM_OFF_CURVE;
}
