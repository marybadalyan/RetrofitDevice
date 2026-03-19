#pragma once

#include <cstdint>

// ── PER-DEVICE CONFIG — change these before each flash ───────
#define DEVICE_ID   "YO4T2S"
#define DEVICE_PASS "j#Lv6pdONQ" 
#define SERVER_URL  "https://nontheoretic-alyce-noncommunistic.ngrok-free.dev"
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
constexpr float kDefaultTargetTemperatureC = 21.0F;

// ── Hub ───────────────────────────────────────────────────────
constexpr const char* kHubHost = "nontheoretic-alyce-noncommunistic.ngrok-free.dev";
constexpr int         kHubPort = 443;  // kept for compatibility but not used in URLs

constexpr uint32_t kHubCommandPollIntervalMs = 2000U;   
constexpr uint32_t kHubTelemetryIntervalMs   = 5000U;  
constexpr int      kHubHttpTimeoutMs         = 3000;

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