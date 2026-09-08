#include "include/control.h"
#include "include/constants.h"

void updateState(State bmsState) {
    switch (bmsState) {
        case Idle:
            break;
        
        case Ready:
            break;

        case Running:
            break;

        case Charging:
            break;

        case Fault:
            break;

        default:
            break;
    }
};

typedef struct {
    float cell_voltages_mv[kModuleCount][kCellsPerModule];
    float module_temps_c[kModuleCount][kThermistorsPerModule];
    uint32_t timestamp_ms;
    
} BmsSnapshot_t;