#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"
#include "protocol.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

class IRReceiver {
public:
    void begin();
    void onEdgeInterrupt();
    bool poll(Command& outCommand);

private:
    static void IRAM_ATTR isrThunk();
    static IRReceiver* activeInstance_;

    std::array<uint16_t, 128> pulseDurationsUs_{};
    volatile size_t pulseCount_ = 0;
    uint32_t lastEdgeUs_ = 0;

    bool decodePacket(protocol::Packet& outPacket, const uint16_t* pulses, size_t pulseCount) const;
};
