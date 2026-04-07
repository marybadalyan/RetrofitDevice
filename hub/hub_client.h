#pragma once

#include <cstdint>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#include "../commands.h"
#include "../logger.h"
#include "../time/wall_clock.h"
#include "hub_receiver.h"
#include "../crypto/message_crypto.h"

class HubClient {
public:
    struct Telemetry {
        float  roomTempC    = 0.0f;
        float  targetTempC  = 0.0f;
        bool   powerOn      = false;
        float  pidP         = 0.0f;
        float  pidI         = 0.0f;
        float  pidD         = 0.0f;
        int8_t pidSteps     = 0;
        float  integral     = 0.0f;
        const char* mode    = "FAST";
    };

    // Custom IR command data (for custom button sending)
    struct PendingCustomIr {
        uint8_t  protocol = 0;
        uint16_t address  = 0;
        uint16_t command  = 0;
        char     name[32] = {};
        bool     valid    = false;
    };

    explicit HubClient(HubReceiver& receiver, Logger& logger);

    void tick(uint32_t nowMs, const WallClockSnapshot& wallNow, bool wifiConnected);
    void submitTelemetry(const Telemetry& telemetry);
    bool hubReachable() const;

    // Returns the latest scheduled target temp from the hub (0 if none received)
    float scheduledTargetTemp() const { return scheduledTargetTemp_; }
    void  clearScheduledTargetTemp()  { scheduledTargetTemp_ = 0.0f; }

    // Returns pending mode change from hub ("FAST", "ECO", or "" if none)
    const char* pendingMode() const   { return pendingMode_[0] ? pendingMode_ : nullptr; }
    void        clearPendingMode()    { pendingMode_[0] = '\0'; }

    // Whether the hub wants the PID auto-control loop to run
    bool autoControl() const          { return autoControl_; }

    // Custom IR: hub resolved a custom button to raw IR data
    bool hasPendingCustomIr() const          { return pendingCustomIr_.valid; }
    PendingCustomIr consumePendingCustomIr() {
        PendingCustomIr ir = pendingCustomIr_;
        pendingCustomIr_.valid = false;
        return ir;
    }

    void forceTelemetry() { lastTelemetryPostMs_ = 0; }
private:
    void pollCommand(const WallClockSnapshot& wallNow);
    void postTelemetry(const WallClockSnapshot& wallNow);
    static Command parseCommandString(const char* str);

#if __has_include(<HTTPClient.h>) && __has_include(<WiFi.h>)
    static bool extractJsonString(const String& payload, const char* key,
                                  char* outValue, size_t outValueSize);
    static bool extractJsonFloat(const String& payload, const char* key,
                                  float& outValue);
    static bool extractJsonBool(const String& payload, const char* key,
                                 bool& outValue);
    static bool extractJsonInt(const String& payload, const char* key,
                                int& outValue);
#endif

    HubReceiver&  receiver_;
    Logger&       logger_;
    MessageCrypto crypto_;

    Telemetry    pendingTelemetry_{};
    bool         hasPendingTelemetry_ = false;
    bool         hubReachable_        = false;

    uint32_t lastCommandPollMs_   = 0;
    uint32_t lastTelemetryPostMs_ = 0;
    float    scheduledTargetTemp_ = 0.0f;
    char     pendingMode_[8]      = {};
    bool     autoControl_         = false;
    PendingCustomIr pendingCustomIr_{};
};