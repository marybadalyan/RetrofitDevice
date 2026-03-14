#include "hub_client.h"
#include "DeviceConfig.h"

#include "../diagnostics/diag.h"
#include "../prefferences.h"

#include <cstring>
#include <cstdio>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if __has_include(<WiFi.h>) && __has_include(<HTTPClient.h>)
#include <WiFi.h>
#include <HTTPClient.h>
#define HUBCLIENT_HAS_HTTP 1
#else
#define HUBCLIENT_HAS_HTTP 0
#endif

// ── Constructor ──────────────────────────────────────────────────────────────

HubClient::HubClient(HubReceiver& receiver, Logger& logger)
    : receiver_(receiver), logger_(logger) {}

// ── Public ───────────────────────────────────────────────────────────────────

void HubClient::tick(uint32_t nowMs,
                     const WallClockSnapshot& wallNow,
                     bool wifiConnected,
                     const DeviceConfig& cfg) {
    // Update host/port from config each tick (handles runtime changes)
    strncpy(hubHost_, cfg.hubHost(), sizeof(hubHost_) - 1);
    hubPort_ = cfg.hubPort();

    if (!wifiConnected) {
        hubReachable_ = false;
        return;
    }

    // Poll for pending hub commands at kHubCommandPollIntervalMs
    if (nowMs - lastCommandPollMs_ >= kHubCommandPollIntervalMs) {
        lastCommandPollMs_ = nowMs;
        pollCommand(wallNow);
    }

    // Post telemetry at kHubTelemetryIntervalMs
    if (hasPendingTelemetry_ &&
        (lastTelemetryPostMs_ == 0U ||
         nowMs - lastTelemetryPostMs_ >= kHubTelemetryIntervalMs)) {
        lastTelemetryPostMs_ = nowMs;
        hasPendingTelemetry_ = !postTelemetry(wallNow);
    }
}

void HubClient::submitTelemetry(const Telemetry& telemetry) {
    pendingTelemetry_    = telemetry;
    hasPendingTelemetry_ = true;
}

bool HubClient::hubReachable() const {
    return hubReachable_;
}

// ── Private: command poll ─────────────────────────────────────────────────────

void HubClient::pollCommand(const WallClockSnapshot& wallNow) {
#if HUBCLIENT_HAS_HTTP
    HTTPClient http;
    http.setConnectTimeout(kHubHttpTimeoutMs);
    http.setTimeout(kHubHttpTimeoutMs);

    // Build URL:  http://<kHubHost>:<kHubPort>/api/command/pending
    char url[128] = {0};
    snprintf(url, sizeof(url), "http://%s:%d/api/command/pending",
             hubHost_, hubPort_);

    if (!http.begin(url)) {
        hubReachable_ = false;
        diag::log(DiagLevel::WARN, "HUB", "command poll: begin() failed");
        return;
    }

    const int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        hubReachable_ = false;
        diag::log(DiagLevel::WARN, "HUB", "command poll: non-200 response");
        return;
    }

    hubReachable_ = true;
    const String payload = http.getString();
    http.end();

    // Parse {"command": "on"} or {"command": null}
    char cmdStr[16] = {0};
    if (!extractJsonString(payload, "command", cmdStr, sizeof(cmdStr))) {
        return;  // null or missing — nothing queued
    }

    if (cmdStr[0] == '\0' || strcmp(cmdStr, "null") == 0) {
        return;  // nothing pending
    }

    const Command cmd = parseCommandString(cmdStr);
    if (cmd == Command::NONE) {
        diag::log(DiagLevel::WARN, "HUB", "command poll: unrecognised command string");
        return;
    }

    // ── LOG 1: command received from hub ─────────────────────────────────────
    // This is the "web sent a command and we received it" log entry.
    logger_.log(wallNow,
                LogEventType::HUB_COMMAND_RX,
                cmd,
                /*success=*/true);

    if (diag::enabled(DiagLevel::INFO)) {
        diag::log(DiagLevel::INFO, "HUB", "Command received from hub");
#if __has_include(<Arduino.h>)
        Serial.print("  cmd=");
        Serial.println(cmdStr);
#endif
    }

    // Push into HubReceiver queue — RetrofitController drains it each tick()
    if (!receiver_.push(cmd)) {
        diag::log(DiagLevel::WARN, "HUB", "command poll: receiver queue full, command dropped");
        logger_.log(wallNow, LogEventType::COMMAND_DROPPED, cmd, /*success=*/false);
    }

#else
    (void)wallNow;
#endif
}

// ── Private: telemetry post ───────────────────────────────────────────────────

bool HubClient::postTelemetry(const WallClockSnapshot& wallNow) {
#if HUBCLIENT_HAS_HTTP
    HTTPClient http;
    http.setConnectTimeout(kHubHttpTimeoutMs);
    http.setTimeout(kHubHttpTimeoutMs);

    char url[128] = {0};
    snprintf(url, sizeof(url), "http://%s:%d/api/telemetry",
             hubHost_, hubPort_);

    if (!http.begin(url)) {
        hubReachable_ = false;
        return false;
    }

    // Build JSON body manually — no ArduinoJson dependency
    char body[256] = {0};
    snprintf(body, sizeof(body),
        "{"
        "\"room_temp\":%.1f,"
        "\"target_temp\":%.1f,"
        "\"power\":%s,"
        "\"mode\":\"%s\","
        "\"pid_p\":%.2f,"
        "\"pid_i\":%.3f,"
        "\"pid_d\":%.2f,"
        "\"pid_steps\":%d,"
        "\"integral\":%.3f"
        "}",
        pendingTelemetry_.roomTempC,
        pendingTelemetry_.targetTempC,
        pendingTelemetry_.powerOn ? "true" : "false",
        pendingTelemetry_.mode ? pendingTelemetry_.mode : "FAST",
        pendingTelemetry_.pidP,
        pendingTelemetry_.pidI,
        pendingTelemetry_.pidD,
        static_cast<int>(pendingTelemetry_.pidSteps),
        pendingTelemetry_.integral
    );

    http.addHeader("Content-Type", "application/json");
    const int httpCode = http.POST(body);

    if (httpCode != 200) {
        http.end();
        hubReachable_ = false;
        diag::log(DiagLevel::WARN, "HUB", "telemetry post: failed");
        return false;
    }

    hubReachable_ = true;

    // Check if hub is pushing a scheduled target override
    const String response = http.getString();
    http.end();

    float scheduledTarget = 0.0f;
    if (extractJsonFloat(response, "scheduled_target", scheduledTarget)) {
        // Hub is overriding target — push a special NONE command as a signal.
        // RetrofitController should check hubClient.scheduledTarget() separately.
        // For now we log it so it appears in the event log.
        logger_.log(wallNow, LogEventType::SCHEDULE_COMMAND, Command::NONE, true);
        diag::log(DiagLevel::INFO, "HUB", "Schedule override received from hub");
#if __has_include(<Arduino.h>)
        if (diag::enabled(DiagLevel::INFO)) {
            Serial.print("  scheduled_target=");
            Serial.println(scheduledTarget, 1);
        }
#endif
    }

    return true;
#else
    (void)wallNow;
    return false;
#endif
}

// ── Private: helpers ──────────────────────────────────────────────────────────

Command HubClient::parseCommandString(const char* str) {
    if (str == nullptr) return Command::NONE;
    if (strcmp(str, "on")        == 0) return Command::ON;
    if (strcmp(str, "off")       == 0) return Command::OFF;
    if (strcmp(str, "temp_up")   == 0) return Command::TEMP_UP;
    if (strcmp(str, "temp_down") == 0) return Command::TEMP_DOWN;
    return Command::NONE;
}

#if HUBCLIENT_HAS_HTTP
bool HubClient::extractJsonString(const String& payload,
                                   const char* key,
                                   char* outValue,
                                   size_t outValueSize) {
    if (key == nullptr || outValue == nullptr || outValueSize == 0U) {
        return false;
    }

    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;

    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;

    // Handle null value: {"command": null}
    const int afterColon = colonPos + 1;
    const String afterColonStr = payload.substring(afterColon);
    // Trim leading spaces
    int i = 0;
    while (i < (int)afterColonStr.length() && afterColonStr[i] == ' ') ++i;
    if (afterColonStr.substring(i, i + 4) == "null") {
        outValue[0] = '\0';
        return false;
    }

    const int startQuote = payload.indexOf('"', colonPos + 1);
    if (startQuote < 0) return false;

    const int endQuote = payload.indexOf('"', startQuote + 1);
    if (endQuote < 0) return false;

    const String value = payload.substring(startQuote + 1, endQuote);
    strncpy(outValue, value.c_str(), outValueSize - 1U);
    outValue[outValueSize - 1U] = '\0';
    return true;
}

bool HubClient::extractJsonFloat(const String& payload,
                                  const char* key,
                                  float& outValue) {
    if (key == nullptr) return false;

    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;

    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;

    // Find the number after the colon
    int numStart = colonPos + 1;
    while (numStart < (int)payload.length() && payload[numStart] == ' ') ++numStart;
    if (numStart >= (int)payload.length()) return false;

    const String numStr = payload.substring(numStart);
    outValue = numStr.toFloat();
    return (outValue != 0.0f || numStr[0] == '0');
}
#endif
