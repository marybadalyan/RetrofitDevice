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

// Wall-clock settings (NTP + local timezone).
constexpr const char* kNtpTimezone = "UTC0";
constexpr const char* kNtpServerPrimary = "pool.ntp.org";
constexpr const char* kNtpServerSecondary = "time.nist.gov";
constexpr const char* kNtpServerTertiary = "time.google.com";
