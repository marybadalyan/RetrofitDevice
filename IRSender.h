#pragma once

#include <cstdint>

#include "IRReciever.h"
#include "commands.h"

class IRSender {
public:
    void begin();
    void sendCommand(Command command);
    void sendAck(Command command);
    void sendRaw(const RawIRFrame& frame, bool startsWithMark = true);

private:
    void mark(uint32_t timeMicros);
    void space(uint32_t timeMicros);
    void sendBit(bool bit);
    void sendByte(uint8_t data);
    void sendFrame(uint8_t commandByte);
};
