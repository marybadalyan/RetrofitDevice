#pragma once

#include <cstdint>

constexpr int kIrTxPin = 4;
constexpr int kIrRxPin = 15;
constexpr int kRelayPin = 5;
constexpr uint8_t kIrPwmChannel = 0;
constexpr uint32_t kIrCarrierFreqHz = 38000;
constexpr uint8_t kIrPwmResolutionBits = 8;

constexpr bool kSchedulerEnabled = true;
constexpr uint32_t kAckTimeoutMs = 120;
constexpr uint8_t kMaxRetryCount = 2;
constexpr float kDefaultTargetTemperatureC = 22.0F;
// +/- deadband around target. Example: target=22C => ON at <=21C, OFF at >=23C.
constexpr float kThermostatHysteresisC = 1.0F;
// Set false if heater control path does not use a physical relay output pin.
constexpr bool kUseRelayOutput = true;

// Retrofit Wi-Fi + hub cloud settings.
constexpr const char* kWifiSsid = "";
constexpr const char* kWifiPassword = "";
constexpr bool kEnableIpTimezoneLookup = true;
constexpr const char* kIpTimezoneUrl = "http://ip-api.com/json/?fields=status,timezone,offset";

// Wall-clock settings (NTP + local timezone).
constexpr const char* kNtpTimezone = "UTC0";
constexpr const char* kNtpServerPrimary = "pool.ntp.org";
constexpr const char* kNtpServerSecondary = "time.nist.gov";
constexpr const char* kNtpServerTertiary = "time.google.com";
