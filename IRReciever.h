#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

struct DecodedFrame {
    Command command = Command::NONE;
    bool isAck = false;
};

struct RawIRFrame {
    std::array<uint16_t, 128> pulsesUs{};
    size_t count = 0;
};

class IRReceiver {
public:
    void begin();
    void onEdgeInterrupt();
    bool poll(DecodedFrame& outFrame);
    bool pollRawFrame(RawIRFrame& outFrame);

private:
    static void IRAM_ATTR isrThunk();
    static IRReceiver* activeInstance_;

    bool decodeFrame(DecodedFrame& outFrame, const RawIRFrame& frame) const;

    std::array<uint16_t, 128> pulseDurationsUs_{};
    RawIRFrame completedFrame_{};
    volatile size_t pulseCount_ = 0;
    volatile uint32_t lastEdgeUs_ = 0;
    volatile bool frameReady_ = false;
};
