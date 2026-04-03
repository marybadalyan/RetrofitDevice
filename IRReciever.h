#pragma once

#include "commands.h"
#include <cstdint>

struct DecodedFrame {
    Command command = Command::NONE;
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
