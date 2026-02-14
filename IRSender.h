#pragma once

#include <cstdint>

#include "commands.h"

class IRSender {
public:
    void begin();
    void sendCommand(Command command);
    void sendAck(Command command);

private:
    void mark(uint32_t timeMicros);
    void space(uint32_t timeMicros);
    void sendBit(bool bit);
    void sendByte(uint8_t data);
    void sendFrame(uint8_t commandByte);
};
