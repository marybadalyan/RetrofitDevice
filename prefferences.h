#pragma once

#include <cstdint>

// GPIO pin for IR transmitter LED (output).
constexpr int kIrTxPin = 4;

// ESP32 LEDC PWM channel used for IR carrier generation. 
constexpr uint8_t kIrPwmChannel = 0;

// GPIO pin for IR receiver module (input/interrupt).
constexpr int kIrRxPin = 15;
// GPIO pin that controls heater relay (heater-side firmware).
constexpr int kRelayPin = 5;

// IR carrier frequency in Hz (standard 38kHz).
constexpr uint32_t kIrCarrierFreqHz = 38000;
// PWM resolution bits for LEDC channel.
constexpr uint8_t kIrPwmResolutionBits = 8;

// Enables autonomous scheduled control mode.
constexpr bool kSchedulerEnabled = true;
// Milliseconds to wait for ACK before retry.
constexpr uint32_t kAckTimeoutMs = 120;
// Number of retries before marking command as failed.
constexpr uint8_t kMaxRetryCount = 2;
// Maximum learning window to capture a raw IR frame.
constexpr uint32_t kLearningTimeoutMs = 15000;
// Enables mock Blynk events for local testing.
constexpr bool kEnableMockBlynk = false;
