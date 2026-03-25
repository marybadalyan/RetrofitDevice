#pragma once

#include "commands.h"

struct DecodedFrame {
    Command command = Command::NONE;
};

class IRReceiver {
public:
    void begin();
    bool poll(DecodedFrame& outFrame);
};
