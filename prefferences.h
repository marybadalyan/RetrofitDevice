#pragma once

#include <cstdint>

constexpr int kIrTxPin = 4;
constexpr int kIrRxPin = 15;
constexpr int kStatusLedRedPin = 18;
constexpr int kStatusLedGreenPin = 19;
constexpr int kStatusLedBluePin = 21;
constexpr bool kStatusLedEnabled = true;
constexpr uint8_t kIrPwmChannel = 0;
constexpr uint32_t kIrCarrierFreqHz = 38000;
constexpr uint8_t kIrPwmResolutionBits = 8;

// ── NEC protocol ─────────────────────────────────────────────
constexpr uint16_t kNecDeviceAddress  = 0x00FF;
constexpr uint8_t  kNecCommandOn      = 0x01;
constexpr uint8_t  kNecCommandOff     = 0x02;
constexpr uint8_t  kNecCommandTempUp  = 0x03;
constexpr uint8_t  kNecCommandTempDown= 0x04;

// ── Thermostat ────────────────────────────────────────────────
constexpr bool  kSchedulerEnabled          = true;
constexpr float kThermostatHysteresisC     = 1.0F;
constexpr int kTempSensorPin = 14;

// ── Diagnostics ───────────────────────────────────────────────
constexpr uint8_t  kDiagnosticsLogLevel       = 2;
constexpr uint32_t kHealthSnapshotIntervalMs   = 10000;
constexpr uint32_t kDefaultTargetTemperatureC = 21.0F;

// ── Hub ───────────────────────────────────────────────────────
constexpr const char* kHubHost = "192.168.0.10";  // ← your Mac IP (ipconfig getifaddr en0)
constexpr int         kHubPort = 5000;

constexpr uint32_t kHubCommandPollIntervalMs = 3000U;  // poll every 10s
constexpr uint32_t kHubTelemetryIntervalMs   = 45000U;  // telemetry every 45s
constexpr int      kHubHttpTimeoutMs         = 3000;

// ── NTP ───────────────────────────────────────────────────────
constexpr bool        kEnableIpTimezoneLookup = true;
constexpr const char* kIpTimezoneUrl          = "http://ip-api.com/json/?fields=status,timezone,offset";

constexpr bool        kEnableHubMockScheduler = true;
constexpr const char* kNtpTimezone            = "UTC0"; 
constexpr const char* kNtpServerPrimary       = "pool.ntp.org";
constexpr const char* kNtpServerSecondary     = "time.nist.gov";
constexpr const char* kNtpServerTertiary      = "time.google.com";
