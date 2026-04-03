#pragma once

#include <cstdint>

// ── PER-DEVICE CONFIG — change these before each flash ───────
#define DEVICE_ID   "YO4T2S"
#define DEVICE_PASS "j#Lv6pdONQ" 
#define SERVER_URL  "http://192.168.0.15:5000"
// ─────────────────────────────────────────────────────────────

constexpr int kIrTxPin = 4;
constexpr int kIrRxPin = 15;
constexpr int kStatusLedRedPin = 18;
constexpr int kStatusLedGreenPin = 19;
constexpr int kStatusLedBluePin = 23;
constexpr bool kStatusLedEnabled = true;
constexpr uint8_t kIrPwmChannel = 0;
constexpr uint32_t kIrCarrierFreqHz = 38000;
constexpr uint8_t kIrPwmResolutionBits = 8;

// ── Thermostat ────────────────────────────────────────────────
constexpr bool  kSchedulerEnabled          = true;
constexpr float kThermostatHysteresisC     = 1.0F;
constexpr int kTempSensorPin = 14;

// ── Diagnostics ───────────────────────────────────────────────
constexpr uint8_t  kDiagnosticsLogLevel       = 2;
constexpr uint32_t kHealthSnapshotIntervalMs   = 10000;
constexpr float kDefaultTargetTemperatureC = 21.0F;

// ── Hub ───────────────────────────────────────────────────────
constexpr const char* kHubHost = "192.168.0.15";
constexpr int         kHubPort = 5000;

constexpr uint32_t kHubCommandPollIntervalMs = 100U;
constexpr uint32_t kHubTelemetryIntervalMs   = 2000U;
constexpr int      kHubHttpTimeoutMs         = 2000;

// ── NTP ───────────────────────────────────────────────────────
constexpr bool        kEnableIpTimezoneLookup = true;
constexpr const char* kIpTimezoneUrl          = "http://ip-api.com/json/?fields=status,timezone,offset";

constexpr bool        kEnableHubMockScheduler = true;
constexpr const char* kNtpTimezone            = "UTC0"; 
constexpr const char* kNtpServerPrimary       = "pool.ntp.org";
constexpr const char* kNtpServerSecondary     = "time.nist.gov";
constexpr const char* kNtpServerTertiary      = "time.google.com";


constexpr int kOledSdaPin = 21;  // I2C SDA
constexpr int kOledSclPin = 22;  // I2C SCL
constexpr uint8_t kOledAddress = 0x3C;  // most common address