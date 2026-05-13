#pragma once

#include "commands.h"
#include <cstdint>

struct DecodedFrame {
    Command  command    = Command::NONE;
    uint8_t  rawCmd     = 0;
    uint16_t rawAddress = 0;
    int      rawProto   = 0;
};

class IRReceiver {
public:
    void begin();
    bool poll(DecodedFrame& outFrame);

private:
    static constexpr uint32_t kDebouncMs = 300;
    Command  lastCmd_   = Command::NONE;
    uint32_t lastCmdMs_ = 0;
};
