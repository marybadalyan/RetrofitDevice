#pragma once

#include <cstdint>

enum class Command : uint8_t {
    NONE = 0x00,
    ON = 0x01,
    OFF = 0x02,
    TEMP_UP = 0x03,
    TEMP_DOWN = 0x04
};

inline const char* commandToString(Command command) {
    switch (command) {
        case Command::ON:
            return "ON";
        case Command::OFF:
            return "OFF";
        case Command::TEMP_UP:
            return "TEMP_UP";
        case Command::TEMP_DOWN:
            return "TEMP_DOWN";
        default:
            return "NONE";
    }
}
