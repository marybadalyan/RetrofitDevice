#pragma once

#include <cstdint>

constexpr int kIrTxPin = 4; // tx transmit
constexpr int kIrRxPin = 15; // rx recieve 
constexpr int kRelayPin = 5;
constexpr int kStatusLedRedPin = 18;
constexpr int kStatusLedGreenPin = 19;
constexpr int kStatusLedBluePin = 21;
constexpr bool kStatusLedEnabled = true;
constexpr uint8_t kIrPwmChannel = 0;
constexpr uint32_t kIrCarrierFreqHz = 38000;
constexpr uint8_t kIrPwmResolutionBits = 8;

// NEC protocol settings
constexpr uint16_t kNecDeviceAddress = 0x00FF;
constexpr uint8_t kNecCommandOn = 0x01;
constexpr uint8_t kNecCommandOff = 0x02;
constexpr uint8_t kNecCommandTempUp = 0x03;
constexpr uint8_t kNecCommandTempDown = 0x04;

constexpr bool kSchedulerEnabled = true;
constexpr float kDefaultTargetTemperatureC = 22.0F;
// +/- deadband around target. Example: target=22C => ON at <=21C, OFF at >=23C.
constexpr float kThermostatHysteresisC = 1.0F;
// Set false if heater control path does not use a physical relay output pin.
constexpr bool kUseRelayOutput = true;
// Serial diagnostics level: 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG.
constexpr uint8_t kDiagnosticsLogLevel = 2;
constexpr uint32_t kHealthSnapshotIntervalMs = 10000;

// Retrofit Wi-Fi + hub cloud settings.w
constexpr const char* kWifiSsid = "";
constexpr const char* kWifiPassword = "";
constexpr bool kEnableIpTimezoneLookup = true;
constexpr const char* kIpTimezoneUrl = "http://ip-api.com/json/?fields=status,timezone,offset";
constexpr bool kEnableHubMockScheduler = false;

// Wall-clock settings (NTP + local timezone).
constexpr const char* kNtpTimezone = "UTC0";
constexpr const char* kNtpServerPrimary = "pool.ntp.org";
constexpr const char* kNtpServerSecondary = "time.nist.gov";
constexpr const char* kNtpServerTertiary = "time.google.com";
