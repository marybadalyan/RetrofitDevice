#pragma once

#include <cstdint>

enum class Command : uint8_t {
    NONE = 0x00,
    ON = 0x01,
    OFF = 0x02,
    TEMP_UP = 0x03,
    TEMP_DOWN = 0x04
};
