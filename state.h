#pragma once

#include <stdint.h>

enum class LampMode : uint8_t {
    AUTO,
    FORCE_ON,
    FORCE_OFF,
};

enum class Presence : uint8_t {
    NONE,
    MOVING,
    STATIONARY,
};

struct LampState {
    LampMode mode;
    Presence presence;
    bool     is_dark;
    uint8_t  brightness;
    uint16_t color_temp;
    uint16_t ldr_raw;
    float    total_kwh;
};
