#pragma once

#include <cstdint>

#include "../commands.h"
#include "../logger.h"
#include "../time/wall_clock.h"
#include "hub_receiver.h"
#include "DeviceConfig.h"

// ── HubClient ────────────────────────────────────────────────────────────────
//
// Handles all HTTP communication with the ThermoHub server.
// Responsibilities:
//   - Poll /api/command/pending and push received commands into HubReceiver
//   - POST telemetry (room temp, PID state, power) to /api/telemetry
//   - Log every stage: command received, command executed, transmit failure
//
// HubConnectivity owns WiFi and NTP — HubClient only runs when WiFi is up.
// Call tick() from the main loop after HubConnectivity::tick().
//
// Usage in main.cpp:
//   HubClient gHubClient(gHubReceiver, gLogger);
//   // in loop():
//   gHubClient.tick(nowMs, wallNow, gHubConnectivity.wifiConnected());

class HubClient {
public:
    // Telemetry snapshot — fill this from your PID result and sensor each cycle.
    struct Telemetry {
        float  roomTempC    = 0.0f;
        float  targetTempC  = 0.0f;
        bool   powerOn      = false;
        float  pidP         = 0.0f;
        float  pidI         = 0.0f;
        float  pidD         = 0.0f;
        int8_t pidSteps     = 0;
        float  integral     = 0.0f;
        const char* mode    = "FAST";   // "FAST" | "ECO"
    };

    explicit HubClient(HubReceiver& receiver, Logger& logger);

    // Call every loop iteration. Only performs HTTP when WiFi is connected
    // and the appropriate interval has elapsed.
    void tick(uint32_t nowMs, const WallClockSnapshot& wallNow, bool wifiConnected, const DeviceConfig& cfg);

    // Call after each PID cycle to queue a telemetry upload.
    // The upload happens on the next tick() when the interval allows.
    void submitTelemetry(const Telemetry& telemetry);

    // Returns true if the hub was reachable on the last poll attempt.
    bool hubReachable() const;

private:
    void pollCommand(const WallClockSnapshot& wallNow);
    bool postTelemetry(const WallClockSnapshot& wallNow);

    static Command parseCommandString(const char* str);

#if __has_include(<HTTPClient.h>) && __has_include(<WiFi.h>)
    static bool extractJsonString(const String& payload,
                                   const char* key,
                                   char* outValue,
                                   size_t outValueSize);
    static bool extractJsonFloat(const String& payload,
                                  const char* key,
                                  float& outValue);
#endif

    HubReceiver& receiver_;
    Logger&      logger_;

    Telemetry    pendingTelemetry_{};
    bool         hasPendingTelemetry_ = false;
    bool         hubReachable_        = false;

    uint32_t lastCommandPollMs_   = 0;
    uint32_t lastTelemetryPostMs_  = 0;
    char     hubHost_[64]          = {};
    int      hubPort_              = 5000;
};
