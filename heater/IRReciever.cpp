#include "IRReciever.h"
#include "prefferences.h"

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
constexpr int CHANGE = 3;
#endif

namespace {
constexpr uint16_t kHeaderMarkUs = 9000;
constexpr uint16_t kHeaderSpaceUs = 4500;
constexpr uint16_t kBitMarkUs = 560;
constexpr uint16_t kBit0SpaceUs = 560;
constexpr uint16_t kBit1SpaceUs = 1690;
constexpr uint16_t kGapResetUs = 15000;
constexpr uint16_t kMinPulseUs = 100;
constexpr uint16_t kToleranceUs = 300;

bool approx(uint16_t value, uint16_t target) {
    return value + kToleranceUs >= target && value <= target + kToleranceUs;
}
}  // namespace

IRReceiver* IRReceiver::activeInstance_ = nullptr;

void IRReceiver::begin() {
    pulseCount_ = 0;
    lastEdgeUs_ = micros();
    pinMode(kIrReceiverPin, INPUT);
    activeInstance_ = this;
    attachInterrupt(digitalPinToInterrupt(kIrReceiverPin), &IRReceiver::isrThunk, CHANGE);
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

    if (deltaUs > kGapResetUs) {
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

bool IRReceiver::decodePacket(protocol::Packet& outPacket, const uint16_t* pulses, size_t pulseCount) const {
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

    uint32_t rawData = 0;
    for (int bit = 0; bit < 32; ++bit) {
        const size_t markIdx = start + static_cast<size_t>(bit) * 2;
        const size_t spaceIdx = markIdx + 1;
        if (spaceIdx >= pulseCount) {
            return false;
        }

        const uint16_t markUs = pulses[markIdx];
        const uint16_t spaceUs = pulses[spaceIdx];
        if (!approx(markUs, kBitMarkUs)) {
            return false;
        }

        rawData <<= 1;
        if (approx(spaceUs, kBit1SpaceUs)) {
            rawData |= 1U;
        } else if (!approx(spaceUs, kBit0SpaceUs)) {
            return false;
        }
    }

    const uint8_t headerHi = static_cast<uint8_t>((rawData >> 24) & 0xFFU);
    const uint8_t headerLo = static_cast<uint8_t>((rawData >> 16) & 0xFFU);
    const uint8_t command = static_cast<uint8_t>((rawData >> 8) & 0xFFU);
    const uint8_t checksum = static_cast<uint8_t>(rawData & 0xFFU);

    outPacket.header = static_cast<uint16_t>((static_cast<uint16_t>(headerHi) << 8) | headerLo);
    outPacket.command = command;
    outPacket.checksum = checksum;
    return true;
}

bool IRReceiver::poll(Command& outCommand) {
    uint16_t localPulses[128]{};
    size_t localCount = 0;
    noInterrupts();
    localCount = pulseCount_;
    if (localCount > pulseDurationsUs_.size()) {
        localCount = pulseDurationsUs_.size();
    }
    for (size_t i = 0; i < localCount; ++i) {
        localPulses[i] = pulseDurationsUs_[i];
    }
    interrupts();

    protocol::Packet packet{};
    if (!decodePacket(packet, localPulses, localCount)) {
        return false;
    }

    Command decoded = Command::NONE;
    if (!protocol::parsePacket(packet, decoded)) {
        return false;
    }

    outCommand = decoded;
    noInterrupts();
    pulseCount_ = 0;
    interrupts();
    return true;
}
