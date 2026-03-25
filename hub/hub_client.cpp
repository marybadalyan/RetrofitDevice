#include "hub_client.h"

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
#include <WiFiClientSecure.h>
#define HUBCLIENT_HAS_HTTP 1
#else
#define HUBCLIENT_HAS_HTTP 0
#endif

HubClient::HubClient(HubReceiver& receiver, Logger& logger)
    : receiver_(receiver), logger_(logger) {}

void HubClient::tick(uint32_t nowMs,
                     const WallClockSnapshot& wallNow,
                     bool wifiConnected) {
    if (!wifiConnected) {
        hubReachable_ = false;
        return;
    }

    if (nowMs - lastCommandPollMs_ >= kHubCommandPollIntervalMs) {
        lastCommandPollMs_ = nowMs;
        pollCommand(wallNow);
    }

    if (hasPendingTelemetry_ &&
        nowMs - lastTelemetryPostMs_ >= kHubTelemetryIntervalMs) {
        lastTelemetryPostMs_ = nowMs;
        postTelemetry(wallNow);
        hasPendingTelemetry_ = false;
    }
}

void HubClient::submitTelemetry(const Telemetry& telemetry) {
    pendingTelemetry_    = telemetry;
    hasPendingTelemetry_ = true;
}

bool HubClient::hubReachable() const {
    return hubReachable_;
}

void HubClient::pollCommand(const WallClockSnapshot& wallNow) {
#if HUBCLIENT_HAS_HTTP
    HTTPClient http;
    http.setConnectTimeout(kHubHttpTimeoutMs);
    http.setTimeout(kHubHttpTimeoutMs);

    char url[128] = {0};
    snprintf(url, sizeof(url), "http://%s:%d/api/command/pending", kHubHost, kHubPort);

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

    // Log successful poll
    static bool firstPoll = true;
    if (firstPoll) {
        firstPoll = false;
        Serial.println("[HUB] Connected to hub successfully!");
    }

    char cmdStr[16] = {0};
    if (!extractJsonString(payload, "command", cmdStr, sizeof(cmdStr))) {
        return;
    }
    if (cmdStr[0] == '\0' || strcmp(cmdStr, "null") == 0) {
        return;
    }

    const Command cmd = parseCommandString(cmdStr);
    if (cmd == Command::NONE) {
        diag::log(DiagLevel::WARN, "HUB", "command poll: unrecognised command");
        return;
    }

    logger_.log(wallNow, LogEventType::HUB_COMMAND_RX, cmd, true);

    Serial.print("[HUB] ✓ Command received and queued: ");
    Serial.println(cmdStr);

    if (!receiver_.push(cmd)) {
        diag::log(DiagLevel::WARN, "HUB", "command poll: queue full, dropped");
        logger_.log(wallNow, LogEventType::COMMAND_DROPPED, cmd, false);
    }
#else
    (void)wallNow;
#endif
}

void HubClient::postTelemetry(const WallClockSnapshot& wallNow) {
#if HUBCLIENT_HAS_HTTP
    HTTPClient http;
    http.setConnectTimeout(kHubHttpTimeoutMs);
    http.setTimeout(kHubHttpTimeoutMs);

    char url[128] = {0};
    snprintf(url, sizeof(url), "http://%s:%d/api/telemetry", kHubHost, kHubPort);

    if (!http.begin(url)) {
        hubReachable_ = false;
        return;
    }

    char body[256] = {0};
    snprintf(body, sizeof(body),
        "{\"room_temp\":%.1f,\"target_temp\":%.1f,\"power\":%s,"
        "\"mode\":\"%s\",\"pid_p\":%.2f,\"pid_i\":%.3f,"
        "\"pid_d\":%.2f,\"pid_steps\":%d,\"integral\":%.3f}",
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
    http.addHeader("X-Device-ID", DEVICE_ID);
    http.addHeader("Authorization", DEVICE_PASS);
    const int httpCode = http.POST(body);

    if (httpCode != 200) {
        http.end();
        hubReachable_ = false;
        return;
    }

    hubReachable_ = true;
    const String response = http.getString();
    http.end();

    float scheduledTarget = 0.0f;
    if (extractJsonFloat(response, "scheduled_target", scheduledTarget)) {
        scheduledTargetTemp_ = scheduledTarget;
        Serial.printf("[HUB] Schedule temp override: %.1f°C\n", scheduledTarget);
        logger_.log(wallNow, LogEventType::SCHEDULE_COMMAND, Command::NONE, true);
    }

    char modeStr[8] = {0};
    if (extractJsonString(response, "pid_mode", modeStr, sizeof(modeStr)) && modeStr[0]) {
        strncpy(pendingMode_, modeStr, sizeof(pendingMode_) - 1);
        pendingMode_[sizeof(pendingMode_) - 1] = '\0';
        Serial.printf("[HUB] Mode change: %s\n", pendingMode_);
    }

    bool autoCtrl = true;
    if (extractJsonBool(response, "auto_control", autoCtrl)) {
        if (autoCtrl != autoControl_) {
            autoControl_ = autoCtrl;
            Serial.printf("[HUB] Auto control: %s\n", autoCtrl ? "ON" : "OFF");
        }
    }
#else
    (void)wallNow;
#endif
}

Command HubClient::parseCommandString(const char* str) {
    if (!str) return Command::NONE;
    if (strcmp(str, "on_off")    == 0) return Command::ON_OFF;
    if (strcmp(str, "on")        == 0) return Command::ON_OFF;  // backward compat
    if (strcmp(str, "off")       == 0) return Command::ON_OFF;  // backward compat
    if (strcmp(str, "temp_up")   == 0) return Command::TEMP_UP;
    if (strcmp(str, "temp_down") == 0) return Command::TEMP_DOWN;
    return Command::NONE;
}

#if HUBCLIENT_HAS_HTTP
bool HubClient::extractJsonString(const String& payload, const char* key,
                                   char* outValue, size_t outValueSize) {
    if (!key || !outValue || outValueSize == 0) return false;
    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;
    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;

    // Handle null
    int i = colonPos + 1;
    while (i < (int)payload.length() && payload[i] == ' ') ++i;
    if (payload.substring(i, i + 4) == "null") {
        outValue[0] = '\0';
        return false;
    }

    const int startQuote = payload.indexOf('"', colonPos + 1);
    if (startQuote < 0) return false;
    const int endQuote = payload.indexOf('"', startQuote + 1);
    if (endQuote < 0) return false;
    const String value = payload.substring(startQuote + 1, endQuote);
    strncpy(outValue, value.c_str(), outValueSize - 1);
    outValue[outValueSize - 1] = '\0';
    return true;
}

bool HubClient::extractJsonBool(const String& payload, const char* key,
                                 bool& outValue) {
    if (!key) return false;
    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;
    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;
    int i = colonPos + 1;
    while (i < (int)payload.length() && payload[i] == ' ') ++i;
    if (payload.substring(i, i + 4) == "true")  { outValue = true;  return true; }
    if (payload.substring(i, i + 5) == "false") { outValue = false; return true; }
    return false;
}

bool HubClient::extractJsonFloat(const String& payload, const char* key,
                                  float& outValue) {
    if (!key) return false;
    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;
    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;
    int numStart = colonPos + 1;
    while (numStart < (int)payload.length() && payload[numStart] == ' ') ++numStart;
    if (numStart >= (int)payload.length()) return false;
    const String numStr = payload.substring(numStart);
    outValue = numStr.toFloat();
    return (outValue != 0.0f || numStr[0] == '0');
}
#endif