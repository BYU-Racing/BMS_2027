#pragma once

/* Cell voltage status values */
#define kCellVoltageErrorMinMv 2800U
#define kCellVoltageWarningMinMv 3100U
#define kCellVoltageGoodMaxMv 4150U
#define kCellVoltageWarningMaxMv 4200

/* Cell temperature status values */
#define kCellTempWarningMinC 5.0
#define kCellGoodMaxC = 50.0f
#define kCellTempWarningMaxC = 60.0f

/* Cell module data */
#define kModuleCount 9U
#define kCellsPerModule 12U
#define kThermistorsPerModule 7U
