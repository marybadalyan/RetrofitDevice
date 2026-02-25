#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if !defined(IRAM_ATTR)
#define IRAM_ATTR
#endif

struct DecodedFrame {
    Command command = Command::NONE;
};

class IRReceiver {
public:
    void begin();
    void onEdgeInterrupt();
    bool poll(DecodedFrame& outFrame);

private:
    static void IRAM_ATTR isrThunk();
    static IRReceiver* activeInstance_;

    bool decodeFrame(DecodedFrame& outFrame, const uint16_t* pulses, size_t pulseCount) const;

    std::array<uint16_t, 128> pulseDurationsUs_{};
    volatile size_t pulseCount_ = 0;
    volatile uint32_t lastEdgeUs_ = 0;
};
