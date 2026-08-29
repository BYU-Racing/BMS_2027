#pragma once

#include "constants.h"

enum class State : uint8_t {
    Idle,
    Ready,
    Running,
    Charging,
    Fault
};

class BMSControl {
public: 
    bool isoSpiConnected = false;
    bool canConnected = false;
    bool moduleConnected = false;
    bool tempSensorsConnected = false;
    bool voltageSensorsConnected = false;

    enum class tempStatus : uint8_t {
        Disconnected,
        Good,
        Warning,
        Error
    };

    enum class voltageStatus : uint8_t {
        Disconnected,
        Good,
        Warning,
        Error
    };

    void initControl();
    void updateState(State bmsState);

private:

};