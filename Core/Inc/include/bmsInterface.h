/* 

Contains the high-level interface between the LTC6811 slave boards and the BMS 

*/
#pragma once

#include "include/constants.h"
#include "ltc6811.h"
#include <stdint.h>

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

