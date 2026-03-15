#include <Arduino.h>
#include <ArduinoJson.h>
#include "diagnostics/diag.h"
#include "hub/hub_connectivity.h"
#include "hub/hub_receiver.h"
#include "hub/hub_client.h"
#include "logger.h"
#include "time/wall_clock.h"
#include "commands.h"
#include "prefferences.h"
#include <HTTPClient.h>
#include <scheduler/scheduler.h>

#include <WiFiManager.h>

namespace {
    HubReceiver     gHubReceiver;
    Logger          gLogger;
    NtpClock        gWallClock;
    HubConnectivity gHubConnectivity;
    HubClient       gHubClient(gHubReceiver, gLogger);
}


bool fetchTimezoneOffset(int32_t& outOffsetSeconds) {
    HTTPClient http;
    http.begin(kIpTimezoneUrl);
    const int code = http.GET();

    if (code != 200) {
        Serial.printf("ip-api failed, HTTP %d\n", code);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    // Parse JSON
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("JSON parse failed");
        return false;
    }

    if (String(doc["status"].as<const char*>()) != "success") {
        Serial.println("ip-api returned non-success");
        return false;
    }

    outOffsetSeconds = doc["offset"].as<int32_t>();
    const char* tz = doc["timezone"].as<const char*>();
    Serial.printf("Detected timezone: %s (offset=%lds)\n", tz, outOffsetSeconds);
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    gLogger.beginPersistence("retrofit-log");

    WiFiManager wifiManager;

const char* portalCSS = R"(
<style>
  @import url('https://fonts.googleapis.com/css2?family=DM+Serif+Display:ital@0;1&family=DM+Mono:wght@400;500&display=swap');
  body{background:#f5f0eb;color:#2a1f14;font-family:'DM Mono',monospace;}
  h1{font-family:'DM Serif Display',serif;font-size:1.5rem;margin-bottom:4px;}
  h1 span{color:#c45c1a;font-style:italic;}
  input{background:#faf7f4;border:1px solid #e8e0d6;border-radius:9px;padding:9px 13px;font-family:'DM Mono',monospace;font-size:0.8rem;color:#2a1f14;width:100%;}
  input:focus{border-color:#c45c1a;outline:none;}
  button{background:#c45c1a;border:none;border-radius:10px;color:#fff;font-family:'DM Mono',monospace;letter-spacing:1.5px;text-transform:uppercase;padding:11px;width:100%;cursor:pointer;}
</style>
)";

wifiManager.setCustomHeadElement(portalCSS);

    wifiManager.resetSettings(); // wipes saved credentials
    wifiManager.autoConnect("ESP32-Setup");
    Serial.println();
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());

    // Fetch timezone offset from IP
    int32_t offsetSeconds = 0;
    if (fetchTimezoneOffset(offsetSeconds)) {
        configTime(offsetSeconds, 0,
                   kNtpServerPrimary,
                   kNtpServerSecondary,
                   kNtpServerTertiary);
    } else {
        Serial.println("[TIME] Falling back to UTC");
        configTime(0, 0,
                   kNtpServerPrimary,
                   kNtpServerSecondary,
                   kNtpServerTertiary);
    }

    // Wait for NTP sync
    Serial.print("[TIME] Waiting for NTP sync");
    time_t now = 0;
    while (now < 1700000000UL) {
        delay(500);
        Serial.print(".");
        time(&now);
    }
    Serial.println();

    // Inject correct time into wallClock
    gWallClock.setUnixTimeMs(static_cast<uint64_t>(now) * 1000ULL, millis());

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    Serial.printf("[TIME] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Tell hub_connectivity WiFi is already up — skip its own begin()
    gHubConnectivity.begin(gHubReceiver, gWallClock);
}


void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();

    gHubConnectivity.tick(nowMs, gHubReceiver, gWallClock);
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

    gHubClient.tick(nowMs, wallNow,
                    gHubConnectivity.wifiConnected());

    Command cmd;
    while (gHubReceiver.poll(cmd)) {
        Serial.print("[CMD] received: ");
        Serial.println(commandToString(cmd));
        gLogger.log(wallNow, LogEventType::COMMAND_SENT, cmd, true);
        switch (cmd) {
            case Command::ON:        Serial.println("  -> heater ON");        break;
            case Command::OFF:       Serial.println("  -> heater OFF");       break;
            case Command::TEMP_UP:   Serial.println("  -> temperature UP");   break;
            case Command::TEMP_DOWN: Serial.println("  -> temperature DOWN"); break;
            default: break;
        }
    }
}