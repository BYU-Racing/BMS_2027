#pragma once

#include <type_traits>

namespace Constants
{
    /* Cell voltage status values */
    constexpr uint16_t kCellVoltageErrorMinMv 2800;
    constexpr uint16_t kCellVoltageWarningMinMv 3100;
    constexpr uint16_t kCellVoltageGoodMaxMv 4150;
    constexpr uint16_t kCellVoltageWarningMaxMv 4200;

    /* Cell temperature status values */
    constexpr float kCellTempWarningMinC 5.0f;
    constexpr float kCellGoodMaxC = 50.0f;
    constexpr float kCellTempWarningMaxC 60.0f;

}