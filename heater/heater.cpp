#include "heater.h"

#include "../prefferences.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
static inline void pinMode(int, int) {}
static inline void digitalWrite(int, int) {}
constexpr int OUTPUT = 1;
constexpr int HIGH = 1;
constexpr int LOW = 0;
#endif

Heater::Heater(float initialTargetTemperatureC)
    : targetTemperatureC_(initialTargetTemperatureC) {}

bool Heater::applyCommand(Command command) {
    switch (command) {
        case Command::ON:
            isOn_ = true;
            return true;
        case Command::OFF:
            isOn_ = false;
            return true;
        case Command::TEMP_UP:
            targetTemperatureC_ += 1.0F;
            return true;
        case Command::TEMP_DOWN:
            targetTemperatureC_ -= 1.0F;
            return true;
        default:
            return false;
    }
}

bool Heater::isOn() const {
    return isOn_;
}

float Heater::targetTemperatureC() const {
    return targetTemperatureC_;
}

bool Heater::shouldHeat(float currentTemperatureC) const {
    return isOn_ && (currentTemperatureC < targetTemperatureC_);
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
