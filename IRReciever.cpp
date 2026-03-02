#include "IRReciever.h"

#include "prefferences.h"
#include "protocol.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#define IR_RX_HAS_ARDUINO 1
#else
#include <cstdio>
#include <cstdint>
#define IR_RX_HAS_ARDUINO 0
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

void IRAM_ATTR IRReceiver::isrThunk() {
    if (activeInstance_ != nullptr) {
        activeInstance_->onEdgeInterrupt();
    }
}

void IRAM_ATTR IRReceiver::onEdgeInterrupt() {
    const uint32_t nowUs = micros();
    const uint32_t deltaUs = nowUs - lastEdgeUs_;
    lastEdgeUs_ = nowUs;

    if (deltaUs > kFrameGapUs) {
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

bool IRReceiver::decodeFrame(DecodedFrame& outFrame, const uint16_t* pulses, size_t pulseCount) const {
    // Pulse timings are interpreted as mark/space pairs. Each pair is one bit.
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

    uint8_t decodedBytes[4] = {0, 0, 0, 0};
    for (int bit = 0; bit < 32; ++bit) {
        const size_t markIdx = start + static_cast<size_t>(bit) * 2;
        const size_t spaceIdx = markIdx + 1;
        if (spaceIdx >= pulseCount) {
            return false;
        }

        if (!approx(pulses[markIdx], kBitMarkUs)) {
            return false;
        }

        const size_t byteIndex = static_cast<size_t>(bit / 8);
        const uint8_t bitMask = static_cast<uint8_t>(1U << (bit % 8));
        if (approx(pulses[spaceIdx], kBit1SpaceUs)) {
            decodedBytes[byteIndex] = static_cast<uint8_t>(decodedBytes[byteIndex] | bitMask);
        } else if (!approx(pulses[spaceIdx], kBit0SpaceUs)) {
            return false;
        }
    }

    protocol::Packet packet{};
    packet.address = decodedBytes[0];
    packet.addressInverse = decodedBytes[1];
    packet.command = decodedBytes[2];
    packet.commandInverse = decodedBytes[3];

    Command cmd = Command::NONE;
    if (!protocol::parsePacket(packet, cmd)) {
        return false;
    }

    outFrame.command = cmd;
    return true;
}

bool IRReceiver::poll(DecodedFrame& outFrame) {
    uint16_t localPulses[128]{};
    size_t count = 0;

    noInterrupts();
    count = pulseCount_;
    if (count > pulseDurationsUs_.size()) {
        count = pulseDurationsUs_.size();
    }
    for (size_t i = 0; i < count; ++i) {
        localPulses[i] = pulseDurationsUs_[i];
    }
    interrupts();

    if (!decodeFrame(outFrame, localPulses, count)) {
        return false;
    }

    if (kDiagnosticsLogLevel >= 2U) {
#if IR_RX_HAS_ARDUINO
        Serial.print("[IR-RX] decoded cmd=");
        Serial.print(commandToString(outFrame.command));
        Serial.println();
#else
        std::printf("[IR-RX] decoded cmd=%s\n", commandToString(outFrame.command));
        std::fflush(stdout);
#endif
    }

    noInterrupts();
    pulseCount_ = 0;
    interrupts();
    return true;
}
