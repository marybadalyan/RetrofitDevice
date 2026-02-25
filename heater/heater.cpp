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

bool Heater::applyCommand(Command command) {
    switch (command) {
        case Command::ON:
            powerEnabled_ = true;
            return true;
        case Command::OFF:
            powerEnabled_ = false;
            return true;
        case Command::TEMP_UP:
        case Command::TEMP_DOWN:
            return true;
        default:
            return false;
    }
}

bool Heater::powerEnabled() const {
    return powerEnabled_;
}

void RelayDriver::begin() {
#if __has_include(<Arduino.h>)
    if (!kUseRelayOutput) {
        return;
    }
#endif
    pinMode(kRelayPin, OUTPUT);
    digitalWrite(kRelayPin, LOW);
}

void RelayDriver::setEnabled(bool enabled) {
#if __has_include(<Arduino.h>)
    if (!kUseRelayOutput) {
        return;
    }
#endif
    digitalWrite(kRelayPin, enabled ? HIGH : LOW);
}

void DisplayDriver::begin() {}

void DisplayDriver::showPowerState(bool isPowerEnabled) {
#if __has_include(<Arduino.h>)
    static uint32_t lastPrintMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastPrintMs < 500) {
        return;
    }
    lastPrintMs = nowMs;

    Serial.print("Heater Power: ");
    Serial.println(isPowerEnabled ? "ON" : "OFF");
#else
    (void)isPowerEnabled;
#endif
}

void CommandStatusLed::begin() {
#if __has_include(<Arduino.h>)
    if (!kStatusLedEnabled) {
        return;
    }
#endif
    pinMode(kStatusLedRedPin, OUTPUT);
    pinMode(kStatusLedGreenPin, OUTPUT);
    pinMode(kStatusLedBluePin, OUTPUT);
    setColor(false, false, false);
}

void CommandStatusLed::showCommand(Command command) {
#if __has_include(<Arduino.h>)
    if (!kStatusLedEnabled) {
        return;
    }
#endif

    switch (command) {
        case Command::ON:
            setColor(false, true, false);   // Green
            break;
        case Command::OFF:
            setColor(true, false, false);   // Red
            break;
        case Command::TEMP_UP:
            setColor(false, false, true);   // Blue
            break;
        case Command::TEMP_DOWN:
            setColor(true, true, false);    // Yellow
            break;
        default:
            setColor(false, false, false);  // Off for unknown commands
            break;
    }
}

void CommandStatusLed::setColor(bool redOn, bool greenOn, bool blueOn) {
    digitalWrite(kStatusLedRedPin, redOn ? HIGH : LOW);
    digitalWrite(kStatusLedGreenPin, greenOn ? HIGH : LOW);
    digitalWrite(kStatusLedBluePin, blueOn ? HIGH : LOW);
}
