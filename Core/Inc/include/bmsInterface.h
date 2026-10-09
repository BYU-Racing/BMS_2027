/* 

Contains the high-level interface between the LTC6811 slave boards and the BMS 

*/
#pragma once

#include "include/constants.h"
#include "ltc6811.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool moduleConnected;
    bool cellVoltageDataValid;
    bool thermistorDataValid;
    uint16_t balanceMask;
    uint16_t cellVoltagesMv[kCellsPerModule];
    float thermistorTempsC[kthermistorsPerModule];

} ModuleData_t;

typedef struct {
    ModuleData_t modules[kNumModules];
} Modules_t;

/* Write the config (REFON = 1, no discharge, no UV/OV compare) to every slave.
   Call once after ltc6811_init(), before the first read. Blocks ~6 ms for the
   reference to settle. */
ltc6811_status_t bmsInterface_initSlaves(ltc6811_t *drv);

/* Start an ADCV conversion, wait for it, read all cell groups and fill
   mods->modules[ic]. IC i in the daisy chain maps to mods->modules[i]. Must be
   called from a FreeRTOS task (uses vTaskDelay). Returns LTC6811_OK,
   LTC6811_ERR_PEC (some data bad, see cellVoltageDataValid) or LTC6811_ERR_SPI.
 */
ltc6811_status_t bmsInterface_updateCellVoltages(ltc6811_t *drv,
                                                 Modules_t *mods);

/* printf every cell voltage of the first nModules modules (needs printf
 * retargeted to a UART). */
void bmsInterface_printCellVoltages(const Modules_t *mods, uint8_t nModules);
