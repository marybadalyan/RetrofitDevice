#pragma once

#include <cstdint>

enum class Command : uint8_t {
    NONE = 0x00,
    ON_OFF = 0x01,
    TEMP_UP = 0x02,
    TEMP_DOWN = 0x03,
    LEARN_ON_OFF   = 0x10,
    LEARN_TEMP_UP  = 0x11,
    LEARN_TEMP_DOWN = 0x12,
    LEARN_CLEAR_ALL = 0x13,
    LEARN_CUSTOM = 0x14,  // Generic learn for custom buttons
};

inline const char* commandToString(Command command) {
    switch (command) {
        case Command::ON_OFF:          return "ON/OFF";
        case Command::TEMP_UP:         return "TEMP_UP";
        case Command::TEMP_DOWN:       return "TEMP_DOWN";
        case Command::LEARN_ON_OFF:    return "LEARN_ON_OFF";
        case Command::LEARN_TEMP_UP:   return "LEARN_TEMP_UP";
        case Command::LEARN_TEMP_DOWN: return "LEARN_TEMP_DOWN";
        case Command::LEARN_CLEAR_ALL: return "LEARN_CLEAR_ALL";
        case Command::LEARN_CUSTOM:    return "LEARN_CUSTOM";
        default:                       return "NONE";
    }
}
