#include <Arduino.h>

#include "diagnostics/diag.h"
#include "hub/hub_connectivity.h"
#include "hub/hub_receiver.h"
#include "hub/HubClient.h"
#include "hub/DeviceConfig.h"
#include "hub/SetupPortal.h"
#include "logger.h"
#include "time/wall_clock.h"
#include "commands.h"

namespace {
    DeviceConfig    gCfg;
    HubReceiver     gHubReceiver;
    Logger          gLogger;
    NtpClock        gWallClock;
    HubConnectivity gHubConnectivity;
    HubClient       gHubClient(gHubReceiver, gLogger);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    gLogger.beginPersistence("retrofit-log");

    // ── Factory reset: hold BOOT button (GPIO0) on power-on ──
    // GPIO0 is the physical BOOT button on ESP32 dev boards.
    // Hold it while powering on to wipe saved WiFi + hub config.
    constexpr int kResetPin = 0;
    pinMode(kResetPin, INPUT_PULLUP);
    if (digitalRead(kResetPin) == LOW) {
        Serial.println("[SETUP] BOOT button held — factory reset");
        Serial.println("[SETUP] Clearing saved config...");
        gCfg.clear();
        Serial.println("[SETUP] Done. Release button and reboot.");
        // Blink forever so user knows to release + reboot
        while (true) { delay(300); }
    }

    // ── Load config from flash ────────────────────────────────
    gCfg.load();

    if (!gCfg.hasWifi()) {
        // ── First boot: no WiFi saved → open setup portal ─────
        Serial.println("[SETUP] No WiFi config found.");
        Serial.println("[SETUP] Connect to 'ThermoSetup' and open http://192.168.4.1");

        SetupPortal portal(gCfg);
        portal.begin();

        // Block here until user saves config — portal reboots ESP32 on save
        while (!portal.done()) {
            portal.tick();
            delay(10);
        }
        // Never reaches here — ESP.restart() called inside portal
        return;
    }

    // ── Normal boot: WiFi credentials exist ──────────────────
    Serial.print("[SETUP] WiFi: ");
    Serial.println(gCfg.wifiSsid());
    Serial.print("[SETUP] Hub:  ");
    Serial.print(gCfg.hubHost());
    Serial.print(":");
    Serial.println(gCfg.hubPort());

    gHubConnectivity.begin(gCfg, gHubReceiver, gWallClock);
}

void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();

    gHubConnectivity.tick(nowMs, gHubReceiver, gWallClock);
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

    gHubClient.tick(nowMs, wallNow,
                    gHubConnectivity.wifiConnected(),
                    gCfg);

    // ── Drain commands from hub and log them ──────────────────
    Command cmd;
    while (gHubReceiver.poll(cmd)) {
        Serial.print("[CMD] received: ");
        Serial.println(commandToString(cmd));

        // HUB_COMMAND_RX already logged inside HubClient::pollCommand()
        // Log execution here
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