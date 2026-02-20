#pragma once

#include <cstdint>

#include "commands.h"

enum class TxFailureCode : uint8_t {
    NONE = 0,
    NOT_INITIALIZED = 1,
    INVALID_COMMAND = 2,
    INVALID_CONFIG = 3,
    HW_UNAVAILABLE = 4
};

class IRSender {
public:
    void begin();
    TxFailureCode sendCommand(Command command);
    TxFailureCode sendAck(Command command);

private:
    void mark(uint32_t timeMicros);
    void space(uint32_t timeMicros);
    void sendBit(bool bit);
    void sendByte(uint8_t data);
    TxFailureCode sendFrame(uint8_t commandByte);
    bool hardwareAvailable_ = true;
    bool initialized_ = false;
};
