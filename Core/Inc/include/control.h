#pragma once

#include "constants.h"

enum State {
    Idle,
    Ready,
    Running,
    Charging,
    Fault
};

void updateState(enum State bmsState);