#include "include/bmsInterface.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

#define REFUP_WAIT_MS     6u    /* tREFUP max 4.4 ms after REFON is set */
#define ADC_CONV_WAIT_MS  4u    /* all cells @ 7 kHz ~2.3 ms; +1 tick so the wait is never short */
#define CELL_RAW_INVALID  0xFFFFu

/* Same config for every IC: reference stays on, no discharge, no UV/OV compare. */
static ltc6811_status_t writeConfig(ltc6811_t *drv)
{
    uint8_t cfg[LTC6811_MAX_IC][LTC6811_REG_BYTES];
    for (uint8_t ic = 0; ic < drv->n_ic; ++ic) {
        ltc6811_pack_cfga(cfg[ic], 1u /*refon*/, 0u /*adcopt*/, 0u, 0u, 0u /*dcc*/, 0u /*dcto*/);
    }
    return ltc6811_write_group(drv, LTC6811_WRCFGA, cfg);
}

static void markAllInvalid(Modules_t *mods, uint8_t n)
{
    for (uint8_t m = 0; m < n; ++m) {
        mods->modules[m].moduleConnected      = false;
        mods->modules[m].cellVoltageDataValid = false;
    }
}

ltc6811_status_t bmsInterface_initSlaves(ltc6811_t *drv)
{
    if (!drv) return LTC6811_ERR_ARG;
    ltc6811_status_t s = writeConfig(drv);
    if (s != LTC6811_OK) return s;
    vTaskDelay(pdMS_TO_TICKS(REFUP_WAIT_MS) + 1u);
    return LTC6811_OK;
}

ltc6811_status_t bmsInterface_updateCellVoltages(ltc6811_t *drv, Modules_t *mods)
{
    if (!drv || !mods) return LTC6811_ERR_ARG;

    const uint8_t nMod = (drv->n_ic < (uint8_t)kNumModules) ? drv->n_ic : (uint8_t)kNumModules;
    uint16_t cv[LTC6811_MAX_IC][LTC6811_CELLS_PER_IC];
    uint16_t failMask = 0;
    ltc6811_status_t s;

    /* Rewriting the config every cycle keeps REFON set and feeds the slave watchdog. */
    s = writeConfig(drv);
    if (s != LTC6811_OK) { markAllInvalid(mods, nMod); return s; }

    /* ADCV: 7 kHz mode, discharge not permitted, all cells */
    s = ltc6811_cmd(drv, LTC6811_ADCV(LTC6811_MD_7KHZ, 0u, 0u));
    if (s != LTC6811_OK) { markAllInvalid(mods, nMod); return s; }

    vTaskDelay(pdMS_TO_TICKS(ADC_CONV_WAIT_MS) + 1u);
    
    s = ltc6811_read_cells(drv, cv, &failMask);
    if (s == LTC6811_ERR_SPI) { markAllInvalid(mods, nMod); return s; }

    for (uint8_t m = 0; m < nMod; ++m) {
        ModuleData_t *mod = &mods->modules[m];
        const bool pecOk = ((failMask >> m) & 1u) == 0u;

        mod->moduleConnected      = pecOk;   /* a silent/absent slave fails PEC on every read */
        mod->cellVoltageDataValid = pecOk;
        if (!pecOk) continue;                /* keep the last values, flag says they're stale */

        for (uint8_t c = 0; c < kCellsPerModule && c < LTC6811_CELLS_PER_IC; ++c) {
            if (cv[m][c] == CELL_RAW_INVALID) { mod->cellVoltageDataValid = false; continue; }
            /* raw LSB = 100 uV -> mV, rounded */
            mod->cellVoltagesMv[c] = (uint16_t)((cv[m][c] + 5u) / 10u);
        }
    }
    return s;   /* OK or ERR_PEC */
}

void bmsInterface_printCellVoltages(const Modules_t *mods, uint8_t nModules)
{
    if (!mods) return;
    if (nModules > (uint8_t)kNumModules) nModules = (uint8_t)kNumModules;

    for (uint8_t m = 0; m < nModules; ++m) {
        const ModuleData_t *mod = &mods->modules[m];
        printf("Module %u: connected=%s, cellV valid=%s\r\n",
               (unsigned)m, mod->moduleConnected ? "yes" : "NO", mod->cellVoltageDataValid ? "yes" : "NO");
        if (!mod->moduleConnected) continue;

        for (uint8_t c = 0; c < kCellsPerModule; ++c) {
            const unsigned mv = mod->cellVoltagesMv[c];
            printf("  Cell %2u: %u.%03u V\r\n", (unsigned)(c + 1u), mv / 1000u, mv % 1000u);
        }
    }
}
