#include "IRReciever.h"

#include "prefferences.h"
#include "protocol.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t micros() { return 0; }
static inline void pinMode(int, int) {}
static inline int digitalPinToInterrupt(int pin) { return pin; }
static inline void attachInterrupt(int, void (*)(), int) {}
static inline void noInterrupts() {}
static inline void interrupts() {}
constexpr int INPUT = 0;
constexpr int CHANGE = 0;
#endif

namespace {
constexpr uint16_t kHeaderMarkUs = 9000;
constexpr uint16_t kHeaderSpaceUs = 4500;
constexpr uint16_t kBitMarkUs = 560;
constexpr uint16_t kBit0SpaceUs = 560;
constexpr uint16_t kBit1SpaceUs = 1690;
constexpr uint16_t kFrameGapUs = 14000;
constexpr uint16_t kMinPulseUs = 80;
constexpr uint16_t kToleranceUs = 300;

bool approx(uint16_t value, uint16_t target) {
    return (value + kToleranceUs >= target) && (value <= target + kToleranceUs);
}
}  // namespace

IRReceiver* IRReceiver::activeInstance_ = nullptr;

void IRReceiver::begin() {
    pulseCount_ = 0;
    lastEdgeUs_ = micros();

    pinMode(kIrRxPin, INPUT);
    activeInstance_ = this;
    attachInterrupt(digitalPinToInterrupt(kIrRxPin), &IRReceiver::isrThunk, CHANGE);
}

void IRReceiver::isrThunk() {
    if (activeInstance_ != nullptr) {
        activeInstance_->onEdgeInterrupt();
    }
}

void IRReceiver::onEdgeInterrupt() {
    const uint32_t nowUs = micros();
    const uint32_t deltaUs = nowUs - lastEdgeUs_;
    lastEdgeUs_ = nowUs;

    if (deltaUs > kFrameGapUs) {
        if (pulseCount_ > 8 && !frameReady_) {
            completedFrame_.count = pulseCount_;
            for (size_t i = 0; i < pulseCount_; ++i) {
                completedFrame_.pulsesUs[i] = pulseDurationsUs_[i];
            }
            frameReady_ = true;
        }
        pulseCount_ = 0;
        return;
    }
    if (deltaUs < kMinPulseUs) {
        return;
    }
    if (pulseCount_ < pulseDurationsUs_.size()) {
        pulseDurationsUs_[pulseCount_] = static_cast<uint16_t>(deltaUs);
        ++pulseCount_;
    }
}

bool IRReceiver::decodeFrame(DecodedFrame& outFrame, const RawIRFrame& frame) const {
    // Pulse timings are interpreted as mark/space pairs. Each pair is one bit.
    const uint16_t* pulses = frame.pulsesUs.data();
    const size_t pulseCount = frame.count;
    if (pulseCount < 66) {
        return false;
    }

    size_t start = 0;
    bool foundHeader = false;
    for (size_t i = 0; i + 1 < pulseCount; ++i) {
        if (approx(pulses[i], kHeaderMarkUs) && approx(pulses[i + 1], kHeaderSpaceUs)) {
            start = i + 2;
            foundHeader = true;
            break;
        }
    }
    if (!foundHeader) {
        return false;
    }

    uint32_t raw = 0;
    for (int bit = 0; bit < 32; ++bit) {
        const size_t markIdx = start + static_cast<size_t>(bit) * 2;
        const size_t spaceIdx = markIdx + 1;
        if (spaceIdx >= pulseCount) {
            return false;
        }

        if (!approx(pulses[markIdx], kBitMarkUs)) {
            return false;
        }

        raw <<= 1;
        if (approx(pulses[spaceIdx], kBit1SpaceUs)) {
            raw |= 1U;
        } else if (!approx(pulses[spaceIdx], kBit0SpaceUs)) {
            return false;
        }
    }

    protocol::Packet packet{};
    packet.header = static_cast<uint16_t>((((raw >> 24) & 0xFFU) << 8) | ((raw >> 16) & 0xFFU));
    packet.command = static_cast<uint8_t>((raw >> 8) & 0xFFU);
    packet.checksum = static_cast<uint8_t>(raw & 0xFFU);

    bool isAck = false;
    Command cmd = Command::NONE;
    if (!protocol::parsePacket(packet, cmd, isAck)) {
        return false;
    }

    outFrame.command = cmd;
    outFrame.isAck = isAck;
    return true;
}

bool IRReceiver::pollRawFrame(RawIRFrame& outFrame) {
    if (!frameReady_) {
        return false;
    }

    noInterrupts();
    outFrame = completedFrame_;
    frameReady_ = false;
    interrupts();
    return outFrame.count > 0;
}

bool IRReceiver::poll(DecodedFrame& outFrame) {
    RawIRFrame frame{};
    if (!pollRawFrame(frame)) {
        return false;
    }

    if (!decodeFrame(outFrame, frame)) {
        return false;
    }
    return true;
}
