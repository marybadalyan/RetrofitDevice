#include "device.h"
#include "prefferences.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>

static inline void delayMicroseconds(uint32_t) {}
static inline uint32_t millis() { return 0; }
static inline void ledcSetup(int, int, int) {}
static inline void ledcAttachPin(int, int) {}
static inline void ledcWrite(int, int) {}
static inline void pinMode(int, int) {}
static inline void digitalWrite(int, int) {}

constexpr int OUTPUT = 1;
constexpr int HIGH = 1;
constexpr int LOW = 0;
#endif

namespace {
constexpr int kPwmChannel = 0;
constexpr int kPwmFreqHz = 38000;
constexpr int kPwmResolutionBits = 8;
}  // namespace

void IRSender::begin() {
    ledcSetup(kPwmChannel, kPwmFreqHz, kPwmResolutionBits);
    ledcAttachPin(kIrSenderPin, kPwmChannel);
}

void IRSender::mark(uint32_t timeMicros) {
    ledcWrite(kPwmChannel, 128);
    delayMicroseconds(timeMicros);
}

void IRSender::space(uint32_t timeMicros) {
    ledcWrite(kPwmChannel, 0);
    delayMicroseconds(timeMicros);
}

void IRSender::sendBit(bool bit) {
    // IR pulse lengths encode bits: fixed mark + variable space.
    mark(560);
    space(bit ? 1690 : 560);
}

void IRSender::sendByte(uint8_t data) {
    for (int i = 7; i >= 0; --i) {
        sendBit((data & (1U << i)) != 0U);
    }
}

void IRSender::sendPacket(uint8_t command) {
    // Packet is sent as header + command + checksum.
    mark(9000);
    space(4500);

    sendByte(static_cast<uint8_t>(protocol::kHeader >> 8));
    sendByte(static_cast<uint8_t>(protocol::kHeader & 0xFF));
    sendByte(command);
    sendByte(protocol::checksum(command));

    mark(560);
    space(560);
}

void IRSender::sendAck(Command command) {
    sendPacket(protocol::makeAck(command));
}

void RelayDriver::begin() {
    pinMode(kRelayPin, OUTPUT);
    digitalWrite(kRelayPin, LOW);
}

void RelayDriver::setEnabled(bool enabled) {
    digitalWrite(kRelayPin, enabled ? HIGH : LOW);
}

void TempSensor::begin() {}

float TempSensor::readTemperatureC() {
    const uint32_t phase = millis() % 5U;
    if (phase == 0U) {
        mockTemperature_ += 0.1F;
    } else if (phase == 3U) {
        mockTemperature_ -= 0.1F;
    }
    return mockTemperature_;
}

void DisplayDriver::begin() {}

void DisplayDriver::showTemperature(float currentTemperatureC, float targetTemperatureC, bool isHeaterOn) {
#if __has_include(<Arduino.h>)
    static uint32_t lastPrintMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastPrintMs < 500) {
        return;
    }
    lastPrintMs = nowMs;

    Serial.print("Current: ");
    Serial.print(currentTemperatureC, 1);
    Serial.print("C Target: ");
    Serial.print(targetTemperatureC, 1);
    Serial.print("C Heater: ");
    Serial.println(isHeaterOn ? "ON" : "OFF");
#else
    (void)currentTemperatureC;
    (void)targetTemperatureC;
    (void)isHeaterOn;
#endif
}
