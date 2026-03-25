#pragma once

#include <cstdint>

enum class Command : uint8_t {
    NONE = 0x00,
    ON_OFF = 0x01,
    TEMP_UP = 0x02,
    TEMP_DOWN = 0x03
};

inline const char* commandToString(Command command) {
    switch (command) {
        case Command::ON_OFF:
            return "ON/OFF";
        case Command::TEMP_UP:
            return "TEMP_UP";
        case Command::TEMP_DOWN:
            return "TEMP_DOWN";
        default:
            return "NONE";
    }
}
