#include "app/adaptive_thermostat_tuning.h"
#include "app/pid_thermostat_controller.h"
#include "commands.h"
#include "diagnostics/diag.h"
#include "hub/hub_client.h"
#include "hub/hub_connectivity.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "prefferences.h"
#include "time/wall_clock.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <scheduler/scheduler.h>
#include <WiFiManager.h>

// ── HARDWARE FLAGS ────────────────────────────────────────────
// Set in platformio.ini build_flags:
//   -DREAL_TEMP_SENSOR   → DS18B20 temperature sensor connected
//   -DREAL_IR_TX       → IR transmitter connected
//   -DREAL_OLED     → 0.96" SSD1306 OLED connected
// No flags = mock room simulation, no hardware needed.

#ifdef REAL_TEMP_SENSOR
#include "room_temp_sensor.h"
#endif

#ifdef REAL_IR_TX
#include "IRSender.h"
#include "IRLearner.h"
#endif

#ifdef REAL_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

// ── MOCK ROOM MODEL ───────────────────────────────────────────
#ifndef REAL_TEMP_SENSOR
namespace MockRoom {
    enum class Season { WINTER, SUMMER };
    constexpr Season kSeason = Season::WINTER;  // ← toggle to test summer

    constexpr float kStartTempC        = (kSeason == Season::WINTER) ? 18.0f : 30.0f;
    constexpr float kOutsideTempC      = (kSeason == Season::WINTER) ? 15.0f : 35.0f;
    constexpr float kHeatingRateCPerMs = 0.000008f;
    constexpr float kCoolingRateCPerMs = 0.000008f;

    float    roomTempC       = kStartTempC;
    float    heaterSetpointC = 21.0f;
    bool     heaterOn        = false;
    uint32_t lastUpdateMs    = 0;

    void applySteps(int8_t steps, float targetTempC) {
        heaterSetpointC += steps * 0.5f;
        if (heaterSetpointC < targetTempC - 2.0f) heaterSetpointC = targetTempC - 2.0f;
        if (heaterSetpointC > targetTempC + 2.0f) heaterSetpointC = targetTempC + 2.0f;
    }

    void update(uint32_t nowMs) {
        if (lastUpdateMs == 0) { lastUpdateMs = nowMs; return; }
        const float dtMs = static_cast<float>(nowMs - lastUpdateMs);
        lastUpdateMs = nowMs;
        if (kSeason == Season::WINTER) {
            if (heaterOn && roomTempC < heaterSetpointC)
                roomTempC += kHeatingRateCPerMs * dtMs;
            else if (roomTempC > kOutsideTempC) {
                roomTempC -= kCoolingRateCPerMs * dtMs;
                if (roomTempC < kOutsideTempC) roomTempC = kOutsideTempC;
            }
        } else {
            if (heaterOn && roomTempC > heaterSetpointC)
                roomTempC -= kHeatingRateCPerMs * dtMs;
            else if (roomTempC < kOutsideTempC) {
                roomTempC += kCoolingRateCPerMs * dtMs;
                if (roomTempC > kOutsideTempC) roomTempC = kOutsideTempC;
            }
        }
    }
} // namespace MockRoom
#endif // REAL_TEMP_SENSOR

// ── PROVISION ────────────────────────────────────────────────
#define PROVISION_BUTTON_GPIO 0
#define HOLD_DURATION_MS      3000

bool should_reprovision() {
    pinMode(PROVISION_BUTTON_GPIO, INPUT_PULLUP);
    Serial.printf("[WIFI] Boot button state: %d\n", digitalRead(PROVISION_BUTTON_GPIO));
    if (digitalRead(PROVISION_BUTTON_GPIO) == LOW) {
        Serial.println("[WIFI] Button held, hold 3s to reprovision...");
        delay(HOLD_DURATION_MS);
        if (digitalRead(PROVISION_BUTTON_GPIO) == LOW) {
            Serial.println("[WIFI] Confirmed.");
            return true;
        }
        Serial.println("[WIFI] Released too early, ignoring.");
    }
    return false;
}

// ── GLOBALS ──────────────────────────────────────────────────
namespace {
    HubReceiver              gHubReceiver;
    Logger                   gLogger;
    NtpClock                 gWallClock;
    HubConnectivity          gHubConnectivity;
    HubClient                gHubClient(gHubReceiver, gLogger);
    CommandScheduler         gCommandScheduler;
    PidThermostatController  gPid;
    AdaptiveThermostatTuning gAdaptive;

    float gTargetTempC   = 21.0f;
    bool  gHeaterPowered = true;

    // Narrative session tracking
    bool              gHeaterWasOn      = false;
    float             gRoomAtOnC        = 0.0f;
    float             gTargetAtOnC      = 0.0f;
    uint32_t          gHeaterOnMs       = 0;
    WallClockSnapshot gHeaterOnSnapshot = {};

    // Last non-zero PID result — for dashboard and OLED
    PidThermostatController::Result gLastPidResult{};

    // Last IR command — for OLED display
    const char* gLastIrCmd   = "none";
    int         gLastIrSteps = 0;

#ifdef REAL_TEMP_SENSOR
    RoomTempSensor gTempSensor;
#endif

#ifdef REAL_IR_TX
    IRSender  gIrSend;
    IRLearner gIrLearner;

    // ── Learn state machine ──────────────────────────────────
    enum class LearnState { IDLE, LISTENING, DONE_OK, DONE_FAIL };
    LearnState gLearnState    = LearnState::IDLE;
    Command    gLearnTarget   = Command::NONE;
    uint32_t   gLearnStartMs  = 0;
    constexpr uint32_t kLearnTimeoutMs = 5000;
#endif

#ifdef REAL_OLED
    Adafruit_SSD1306 gDisplay(128, 64, &Wire, -1);
#endif
} // namespace

// ── OLED UPDATE ───────────────────────────────────────────────
#ifdef REAL_OLED
void updateDisplay(float roomTempC, float targetTempC, bool heaterOn) {
    gDisplay.clearDisplay();
    gDisplay.setTextColor(SSD1306_WHITE);

    // Row 1: big room temp + power state
    gDisplay.setTextSize(2);
    gDisplay.setCursor(0, 0);
    gDisplay.printf("%.1fC", roomTempC);
    gDisplay.setTextSize(1);
    gDisplay.setCursor(90, 4);
    gDisplay.print(heaterOn ? "[ ON ]" : "[OFF]");

    // Row 2: target
    gDisplay.setCursor(0, 20);
    gDisplay.printf("Target: %.1fC", targetTempC);

    // Row 3: last IR command
    gDisplay.setCursor(0, 32);
    if (gLastIrSteps != 0) {
        gDisplay.printf("IR: %s x%d", gLastIrCmd, abs(gLastIrSteps));
    } else {
        gDisplay.print("IR: idle");
    }

    // Row 4: PID values
    gDisplay.setCursor(0, 44);
    gDisplay.printf("P:%.1f I:%.2f D:%.1f",
                    gLastPidResult.p, gLastPidResult.i, gLastPidResult.d);

    // Row 5: steps
    gDisplay.setCursor(0, 54);
    gDisplay.printf("Steps: %+d", gLastPidResult.steps);

    gDisplay.display();
}
#endif

// ── NARRATIVE LOGGING ────────────────────────────────────────
void onHeaterTurnedOn(uint32_t nowMs, const WallClockSnapshot& wallNow,
                      float roomTempC, float targetTempC) {
    gHeaterWasOn      = true;
    gRoomAtOnC        = roomTempC;
    gTargetAtOnC      = targetTempC;
    gHeaterOnMs       = nowMs;
    gHeaterOnSnapshot = wallNow;
    Serial.printf("[HEAT] ON  at %02d:%02d — room %.1f°C, target %.1f°C\n",
                  wallNow.hour, wallNow.minute, roomTempC, targetTempC);
    gLogger.log(wallNow, LogEventType::STATE_CHANGE, Command::ON_OFF, true);
}

void onHeaterTurnedOff(uint32_t nowMs, const WallClockSnapshot& wallNow,
                       float roomTempC) {
    if (!gHeaterWasOn) return;
    const uint32_t durationMs  = nowMs - gHeaterOnMs;
    const uint32_t durationMin = durationMs / 60000UL;
    const uint32_t durationSec = (durationMs % 60000UL) / 1000UL;
    const float    rise        = roomTempC - gRoomAtOnC;
    Serial.printf("[HEAT] OFF at %02d:%02d — ran %um%02us | %.1f°C → %.1f°C (%.1f° rise) | target was %.1f°C\n",
                  wallNow.hour, wallNow.minute, durationMin, durationSec,
                  gRoomAtOnC, roomTempC, rise, gTargetAtOnC);
    gLogger.log(wallNow, LogEventType::STATE_CHANGE, Command::ON_OFF, true);
    gHeaterWasOn = false;
}

// ── TIMEZONE FETCH ───────────────────────────────────────────
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

// ── PORTAL CSS ───────────────────────────────────────────────
const char* portalCSS = R"(
<style>
  @import url('https://fonts.googleapis.com/css2?family=DM+Serif+Display:ital@0;1&family=DM+Mono:wght@400;500&display=swap');
  body{background:#f5f0eb;color:#2a1f14;font-family:'DM Mono',monospace;}
  h1{font-family:'DM Serif Display',serif;font-size:1.5rem;margin-bottom:4px;}
  h1 span{color:#c06c84;font-style:italic;}
  input{background:#faf7f4;border:1px solid #e8e0d6;border-radius:9px;padding:9px 13px;font-family:'DM Mono',monospace;font-size:0.8rem;color:#2a1f14;width:100%;}
  input:focus{border-color:#c06c84;outline:none;}
  button{background:#c06c84;border:none;border-radius:10px;color:#fff;font-family:'DM Mono',monospace;letter-spacing:1.5px;text-transform:uppercase;padding:11px;width:100%;cursor:pointer;}
</style>
)";

// ── SETUP ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

#ifdef DEV_WIFI_SSID
    Serial.println("[WIFI] Dev mode: connecting with hardcoded credentials...");
    WiFi.begin(DEV_WIFI_SSID, DEV_WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println();
#else
    bool reprovision = should_reprovision();
    gLogger.beginPersistence("thermoDevice-log");
    WiFiManager wifiManager;

    wifiManager.setCustomHeadElement(portalCSS);

    if (reprovision) {
        Serial.println("[WIFI] Reprovisioning requested, wiping credentials...");
        wifiManager.resetSettings();
    }
    wifiManager.autoConnect("ESP32-Setup");
    Serial.printf("[WIFI] Connected — dashboard: http://%s:%d/device/%s\n", kHubHost, kHubPort, DEVICE_ID);
#endif

    Serial.println();
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());

    int32_t offsetSeconds = 0;
    if (fetchTimezoneOffset(offsetSeconds)) {
        configTime(offsetSeconds, 0, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);
    } else {
        Serial.println("[TIME] Falling back to UTC");
        configTime(0, 0, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);
    }

    Serial.print("[TIME] Waiting for NTP sync");
    time_t now = 0;
    while (now < 1700000000UL) { delay(500); Serial.print("."); time(&now); }
    Serial.println();

    gWallClock.setUnixTimeMs(static_cast<uint64_t>(now) * 1000ULL, millis());
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    Serial.printf("[TIME] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    gHubConnectivity.begin(gHubReceiver, gWallClock);
    gCommandScheduler.setEnabled(true);

#ifndef REAL_TEMP_SENSOR
    gTargetTempC = MockRoom::roomTempC;
    gPid.reset(MockRoom::roomTempC);
    Serial.printf("[MOCK] Season: %s | start=%.1f°C outside=%.1f°C target=%.1f°C (synced to room)\n",
                  MockRoom::kSeason == MockRoom::Season::WINTER ? "WINTER" : "SUMMER",
                  MockRoom::kStartTempC, MockRoom::kOutsideTempC, gTargetTempC);
#else
    gTempSensor.begin();
    const float initialTempC = gTempSensor.readTemperatureC();
    gTargetTempC = initialTempC;
    gPid.reset(initialTempC);
    Serial.printf("[SENSOR] DS18B20 active. Initial temp: %.1f°C (target synced)\n", initialTempC);
#endif

#ifdef REAL_IR_TX
    gIrLearner.begin();
    gIrSend.setLearner(&gIrLearner);  // must be before begin() — begin() calls learner_->beginSend()
    gIrSend.begin();
    Serial.printf("[IR] Transmitter ready on GPIO %d\n", kIrTxPin);
    Serial.printf("[IR] Learner ready (rx GPIO %d). Learned codes: ON=%s UP=%s DN=%s\n",
                  kIrRxPin,
                  gIrLearner.hasLearned(Command::ON_OFF)    ? "yes" : "no",
                  gIrLearner.hasLearned(Command::TEMP_UP)   ? "yes" : "no",
                  gIrLearner.hasLearned(Command::TEMP_DOWN) ? "yes" : "no");
#endif

#ifdef REAL_OLED
    Wire.begin(kOledSdaPin, kOledSclPin);
    if (!gDisplay.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
        Serial.println("[OLED] Display not found!");
    } else {
        gDisplay.clearDisplay();
        gDisplay.setTextColor(SSD1306_WHITE);
        gDisplay.setTextSize(1);
        gDisplay.setCursor(0, 0);
        gDisplay.println("ThermoHub");
        gDisplay.println("Starting...");
        gDisplay.display();
        Serial.println("[OLED] Display ready.");
    }
#endif
}

// ── LEARN RESULT POST ─────────────────────────────────────────
#ifdef REAL_IR_TX
static bool postLearnResult(Command cmd, bool success) {
    const char* cmdStr = "on_off";
    if (cmd == Command::TEMP_UP)       cmdStr = "temp_up";
    if (cmd == Command::TEMP_DOWN)     cmdStr = "temp_down";
    if (cmd == Command::LEARN_CUSTOM)  cmdStr = "learn_custom";

    char body[256];
    if (cmd == Command::LEARN_CUSTOM && success) {
        LearnedCode code;
        if (gIrLearner.getLastCaptured(code)) {
            snprintf(body, sizeof(body),
                "{\"cmd\":\"%s\",\"status\":\"ok\","
                "\"protocol\":%d,\"address\":%d,\"command\":%d}",
                cmdStr, code.protocol, code.address, code.command);
        } else {
            snprintf(body, sizeof(body), "{\"cmd\":\"%s\",\"status\":\"ok\"}", cmdStr);
        }
    } else {
        snprintf(body, sizeof(body), "{\"cmd\":\"%s\",\"status\":\"%s\"}",
                 cmdStr, success ? "ok" : "fail");
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/learn/result", kHubHost, kHubPort);

    // Retry up to 5 times — WiFi stack needs time to recover after the
    // tight IR polling loop where zero WiFi calls were made.
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) delay(500);

        WiFiClient wifiClient;
        HTTPClient http;
        if (!http.begin(wifiClient, url)) {
            Serial.printf("[LEARN] http.begin() failed (attempt %d/5)\n", attempt + 1);
            continue;
        }
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-Device-ID", DEVICE_ID);
        http.addHeader("Authorization", DEVICE_PASS);
        http.setTimeout(2000);

        const int code = http.POST(body);
        http.end();
        if (code == 200) {
            Serial.printf("[LEARN] Reported %s=%s to hub\n", cmdStr, success ? "ok" : "fail");
            return true;
        }
        Serial.printf("[LEARN] POST failed HTTP %d (attempt %d/5)\n", code, attempt + 1);
    }
    Serial.println("[LEARN] All POST attempts failed");
    return false;
}
#endif

// ── LOOP ─────────────────────────────────────────────────────
void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    static uint32_t lastTelemetryMs = 0;

    // ── 1. Connectivity + time ───────────────────────────────
    gHubConnectivity.tick(nowMs, gHubReceiver, gWallClock);
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

    // ── 2. Read room temperature ──────────────────────────────
#ifndef REAL_TEMP_SENSOR
    MockRoom::heaterOn = gHeaterPowered;
    MockRoom::update(nowMs);
    const float roomTempC = MockRoom::roomTempC;
#else
    const float roomTempC = gTempSensor.readTemperatureC();
#endif

    // ── 0. IR learn state machine ────────────────────────────
    // LISTENING: pure IR polling — no WiFi calls at all (WiFi.status()
    // at tight-loop speed corrupts the WiFi driver after a few seconds).
#ifdef REAL_IR_TX
    if (gLearnState == LearnState::LISTENING) {
        const LearnPollResult lpr = gIrLearner.poll(gLearnTarget);
        if (lpr == LearnPollResult::OK) {
            gLearnState = LearnState::DONE_OK;
            gIrLearner.stopListen();
            Serial.printf("[LEARN] Success for %s\n", commandToString(gLearnTarget));
        } else if (nowMs - gLearnStartMs >= kLearnTimeoutMs) {
            gLearnState = LearnState::DONE_FAIL;
            gIrLearner.stopListen();
            Serial.printf("[LEARN] Timeout for %s\n", commandToString(gLearnTarget));
        }
        return;   // pure IR — nothing else
    }

    // DONE: learning finished, post result to hub.
    // Runs on the NEXT iteration after LISTENING exits.
    // delay(500) lets the WiFi driver fully recover after seconds of
    // zero WiFi calls in the tight IR polling loop.
    if (gLearnState == LearnState::DONE_OK || gLearnState == LearnState::DONE_FAIL) {
        Serial.println("[LEARN] Posting result to hub…");

        // Give the WiFi radio more time to "wake up" after the tight loop
        delay(500);

        const bool ok = (gLearnState == LearnState::DONE_OK);

        // We MUST successfully POST the custom IR data, or the button will be empty (0,0,0)
        if (postLearnResult(gLearnTarget, ok)) {
            gLearnState = LearnState::IDLE;
            // Force a sync so the Hub knows the 'learn_custom' command is finished
            gHubClient.tick(millis(), wallNow, true);
        } else {
            // If it fails after retries, reset to avoid an infinite loop.
            gLearnState = LearnState::IDLE;
        }
    }
#endif

    // ── 3. Apply mode change from hub ─────────────────────────
    const char* pendingMode = gHubClient.pendingMode();
    if (pendingMode) {
        if (strcmp(pendingMode, "ECO") == 0) {
            gPid.setMode(ThermostatMode::ECO);
            Serial.println("[MODE] Switched to ECO");
        } else if (strcmp(pendingMode, "FAST") == 0) {
            gPid.setMode(ThermostatMode::FAST);
            Serial.println("[MODE] Switched to FAST");
        }
        gHubClient.clearPendingMode();
    }

    // ── 4. Apply scheduled target from hub ────────────────────
    const float scheduledTemp = gHubClient.scheduledTargetTemp();
    if (scheduledTemp > 0.0f) {
        Serial.printf("[SCHED] Target updated: %.1f°C → %.1f°C\n", gTargetTempC, scheduledTemp);
        gTargetTempC = scheduledTemp;
        gHubClient.clearScheduledTargetTemp();
    }

    // ── 5. Hub tick ───────────────────────────────────────────
    gHubClient.tick(nowMs, wallNow, gHubConnectivity.wifiConnected());

    // ── 6. Adaptive tuning ────────────────────────────────────
    const AdaptiveThermostatTuning::Overrides overrides = gAdaptive.update(
        nowMs, roomTempC, gPid.mode(), gPid.baseTuningForMode(gPid.mode()));
    PidThermostatController::RuntimeOverrides rtOverrides;
    rtOverrides.enabled  = true;
    rtOverrides.kp       = overrides.kp;
    rtOverrides.maxSteps = overrides.maxSteps;
    gPid.setRuntimeOverrides(rtOverrides);

    // ── 7. PID tick (only when heater is on and auto-control enabled) ────
    PidThermostatController::Result pidResult{};

    if (gHeaterPowered && gHubClient.autoControl()) {
        pidResult = gPid.tick(nowMs, gTargetTempC, roomTempC);

        if (pidResult.ranControlCycle) {
            Serial.printf("[PID] room=%.2f°C target=%.2f°C err=%+.2f "
                          "P=%+.2f I=%+.3f D=%+.2f steps=%+d kp=%.2f maxSteps=%d\n",
                          roomTempC, gTargetTempC, pidResult.errorC,
                          pidResult.p, pidResult.i, pidResult.d, pidResult.steps,
                          overrides.kp, overrides.maxSteps);

            gLogger.log(wallNow, LogEventType::THERMOSTAT_CONTROL, Command::NONE, true,
                        static_cast<uint8_t>(pidResult.steps < 0 ? 0 : pidResult.steps));

            if (pidResult.steps != 0) {
                gLastPidResult = pidResult;
                gLastIrCmd     = pidResult.steps > 0 ? "TEMP_UP" : "TEMP_DOWN";
                gLastIrSteps   = pidResult.steps;

#ifndef REAL_TEMP_SENSOR
                MockRoom::applySteps(pidResult.steps, gTargetTempC);
                Serial.printf("[MOCK] heaterSetpoint now %.1f°C\n", MockRoom::heaterSetpointC);
#endif
                gAdaptive.onControlStepsSent(nowMs, roomTempC, pidResult.steps);

#ifdef REAL_IR_TX
                const Command irCmd = pidResult.steps > 0 ? Command::TEMP_UP : Command::TEMP_DOWN;
                for (int s = 0; s < abs(pidResult.steps); s++) {
                    gIrSend.sendCommand(irCmd);
                    delay(50);
                }
                Serial.printf("[IR] Sent %s x%d\n",
                              gLastIrCmd, abs(pidResult.steps));
#else
                Serial.printf("[IR]  -> %s x%d\n", gLastIrCmd, abs(pidResult.steps));
#endif
            }
        }
    }

    // ── 8. Idle log when PID is off ───────────────────────────
    if (!gHubClient.autoControl()) {
        if (pidResult.ranControlCycle || (nowMs - lastTelemetryMs >= 10000)) {
            Serial.printf("[IDLE] room=%.2f°C target=%.2f°C power=%s\n",
                          roomTempC, gTargetTempC, gHeaterPowered ? "ON" : "OFF");
        }
    }

    // ── 8. Telemetry every 10s ────────────────────────────────
    if (nowMs - lastTelemetryMs >= 10000) {
        lastTelemetryMs = nowMs;
        HubClient::Telemetry t;
        t.roomTempC   = roomTempC;
        t.targetTempC = gTargetTempC;
        t.powerOn     = gHeaterPowered;
        t.mode        = (gPid.mode() == ThermostatMode::ECO) ? "ECO" : "FAST";
        t.pidP        = gLastPidResult.p;
        t.pidI        = gLastPidResult.i;
        t.pidD        = gLastPidResult.d;
        t.pidSteps    = gLastPidResult.steps;
        t.integral    = gLastPidResult.i;
        gHubClient.submitTelemetry(t);
    }

    // ── 9. OLED update ────────────────────────────────────────
#ifdef REAL_OLED
    updateDisplay(roomTempC, gTargetTempC, gHeaterPowered);
#endif

    // ── 10. Local scheduler ───────────────────────────────────
    Command scheduledCmd;
    if (gCommandScheduler.nextDueCommand(nowMs, wallNow, scheduledCmd)) {
        Serial.printf("[SCHED] Firing: %s\n", commandToString(scheduledCmd));
        gHubReceiver.push(scheduledCmd);
    }

    // ── 11a. Send custom IR (from custom buttons) ─────────────
#ifdef REAL_IR_TX
    if (gHubClient.hasPendingCustomIr()) {
        auto ir = gHubClient.consumePendingCustomIr();
        gIrLearner.sendCodeDirect(ir.protocol, ir.address, ir.command);
        Serial.printf("[IR] Sent \"%s\": proto=%d addr=0x%04X cmd=0x%04X\n",
                      ir.name[0] ? ir.name : "custom",
                      ir.protocol, ir.address, ir.command);
    }
#endif

    // ── 11. Execute commands ──────────────────────────────────
    Command cmd;
    while (gHubReceiver.poll(cmd)) {
        Serial.printf("[CMD] %s  target=%.1f°C  room=%.1f°C\n", commandToString(cmd), gTargetTempC, roomTempC);
        gLogger.log(wallNow, LogEventType::COMMAND_SENT, cmd, true);

        switch (cmd) {
        case Command::ON_OFF:
            gHeaterPowered = !gHeaterPowered;
#ifdef REAL_IR_TX
            gIrSend.sendCommand(Command::ON_OFF);
            Serial.printf("[IR] Sent ON/OFF\n");
#endif
            if (gHeaterPowered) {
#ifndef REAL_TEMP_SENSOR
                MockRoom::heaterSetpointC = gTargetTempC;
#endif
                gPid.reset(roomTempC);
                onHeaterTurnedOn(nowMs, wallNow, roomTempC, gTargetTempC);
            } else {
                onHeaterTurnedOff(nowMs, wallNow, roomTempC);
            }
            break;

        case Command::TEMP_UP:
            if (!gHeaterPowered) { Serial.println("[CMD] Ignored TEMP_UP — heater is off"); break; }
            if (gHubClient.autoControl()) {
                // PID mode: shift target, PID will handle IR
                gTargetTempC += 0.5f;
                Serial.printf("[CMD] PID target -> %.1f C\n", gTargetTempC);
                gHubClient.forceTelemetry();
            } else {
                // Manual mode: send IR directly, but keep gTargetTempC in sync
                gTargetTempC += 0.5f;
#ifdef REAL_IR_TX
                gIrSend.sendCommand(Command::TEMP_UP);
#else
                MockRoom::heaterSetpointC += 0.5f;
#endif
                Serial.printf("[CMD] Manual TEMP_UP — IR sent directly, target=%.1f\n", gTargetTempC);
                gHubClient.forceTelemetry();
            }
            break;

        case Command::TEMP_DOWN:
            if (!gHeaterPowered) { Serial.println("[CMD] Ignored TEMP_DOWN — heater is off"); break; }
            if (gHubClient.autoControl()) {
                // PID mode: shift target, PID will handle IR
                gTargetTempC -= 0.5f;
                Serial.printf("[CMD] PID target -> %.1f C\n", gTargetTempC);
                gHubClient.forceTelemetry();
            } else {
                // Manual mode: send IR directly, but keep gTargetTempC in sync
                gTargetTempC -= 0.5f;
#ifdef REAL_IR_TX
                gIrSend.sendCommand(Command::TEMP_DOWN);
#else
                MockRoom::heaterSetpointC -= 0.5f;
#endif
                Serial.printf("[CMD] Manual TEMP_DOWN — IR sent directly, target=%.1f\n", gTargetTempC);
                gHubClient.forceTelemetry();
            }
            break;

        case Command::LEARN_ON_OFF:
        case Command::LEARN_TEMP_UP:
        case Command::LEARN_TEMP_DOWN:
        case Command::LEARN_CUSTOM:
#ifdef REAL_IR_TX
            if (gLearnState == LearnState::LISTENING) {
                // Already learning — cancel and restart for new target
                gIrLearner.stopListen();
            }
            if (cmd == Command::LEARN_ON_OFF)    gLearnTarget = Command::ON_OFF;
            else if (cmd == Command::LEARN_TEMP_UP)  gLearnTarget = Command::TEMP_UP;
            else if (cmd == Command::LEARN_TEMP_DOWN)  gLearnTarget = Command::TEMP_DOWN;
            else if (cmd == Command::LEARN_CUSTOM)    gLearnTarget = Command::LEARN_CUSTOM;
            gLearnState   = LearnState::LISTENING;
            gLearnStartMs = nowMs;
            gIrLearner.beginListen();
            Serial.printf("[LEARN] Started listening for %s (%lus timeout)\n",
                          commandToString(cmd),
                          static_cast<unsigned long>(kLearnTimeoutMs / 1000));
#endif
            break;

        case Command::LEARN_CLEAR_ALL:
#ifdef REAL_IR_TX
            gIrLearner.clearAll();
            Serial.println("[LEARN] All codes cleared.");
#endif
            break;

        default:
            break;
        }
    }
}