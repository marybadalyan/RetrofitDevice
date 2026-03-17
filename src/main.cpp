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

// ── MOCK ROOM MODEL ───────────────────────────────────────────
// Simulates a real room heating/cooling so PID + adaptive can
// be tested without a physical sensor.
// Swap roomTempC with a real sensor read when hardware is ready.
namespace MockRoom {
constexpr float kStartTempC = 18.0f;
constexpr float kOutsideTempC = 15.0f;
constexpr float kHeatingRateCPerMs = 0.000008f;
constexpr float kCoolingRateCPerMs = 0.000008f;

float roomTempC = kStartTempC;
float heaterSetpointC = 21.0f;
bool heaterOn = false;
uint32_t lastUpdateMs = 0;

void applySteps(int8_t steps, float targetTempC) {
  heaterSetpointC += steps * 0.5f;
  if (heaterSetpointC < targetTempC - 2.0f)
    heaterSetpointC = targetTempC - 2.0f;
  if (heaterSetpointC > targetTempC + 2.0f)
    heaterSetpointC = targetTempC + 2.0f;
}
void update(uint32_t nowMs) {
  if (lastUpdateMs == 0) {
    lastUpdateMs = nowMs;
    return;
  }
  const float dtMs = static_cast<float>(nowMs - lastUpdateMs);
  lastUpdateMs = nowMs;
  if (heaterOn && roomTempC < heaterSetpointC) {
    roomTempC += kHeatingRateCPerMs * dtMs;
  } else if (roomTempC > kOutsideTempC) {
    roomTempC -= kCoolingRateCPerMs * dtMs;
    if (roomTempC < kOutsideTempC)
      roomTempC = kOutsideTempC;
  }
}
} // namespace MockRoom
#define PROVISION_BUTTON_GPIO 0
#define HOLD_DURATION_MS 3000

bool should_reprovision() {
  pinMode(PROVISION_BUTTON_GPIO, INPUT_PULLUP);
  Serial.printf("[WIFI] Boot button state: %d\n",
                digitalRead(PROVISION_BUTTON_GPIO));
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
HubReceiver gHubReceiver;
Logger gLogger;
NtpClock gWallClock;
HubConnectivity gHubConnectivity;
HubClient gHubClient(gHubReceiver, gLogger);
CommandScheduler gCommandScheduler;
PidThermostatController gPid;
AdaptiveThermostatTuning gAdaptive;

float gTargetTempC = 21.0f;
bool gHeaterPowered = true;

// Narrative session tracking
bool gHeaterWasOn = false;
float gRoomAtOnC = 0.0f;
float gTargetAtOnC = 0.0f;
uint32_t gHeaterOnMs = 0;
WallClockSnapshot gHeaterOnSnapshot = {};
} // namespace

// ── NARRATIVE LOGGING ────────────────────────────────────────
void onHeaterTurnedOn(uint32_t nowMs, const WallClockSnapshot &wallNow,
                      float roomTempC, float targetTempC) {
  gHeaterWasOn = true;
  gRoomAtOnC = roomTempC;
  gTargetAtOnC = targetTempC;
  gHeaterOnMs = nowMs;
  gHeaterOnSnapshot = wallNow;

  Serial.printf("[HEAT] ON  at %02d:%02d — room %.1f°C, target %.1f°C\n",
                wallNow.hour, wallNow.minute, roomTempC, targetTempC);
  gLogger.log(wallNow, LogEventType::STATE_CHANGE, Command::ON, true);
}

void onHeaterTurnedOff(uint32_t nowMs, const WallClockSnapshot &wallNow,
                       float roomTempC) {
  if (!gHeaterWasOn)
    return;

  const uint32_t durationMs = nowMs - gHeaterOnMs;
  const uint32_t durationMin = durationMs / 60000UL;
  const uint32_t durationSec = (durationMs % 60000UL) / 1000UL;
  const float rise = roomTempC - gRoomAtOnC;

  Serial.printf("[HEAT] OFF at %02d:%02d — ran %um%02us | %.1f°C → %.1f°C "
                "(%.1f° rise) | target was %.1f°C\n",
                wallNow.hour, wallNow.minute, durationMin, durationSec,
                gRoomAtOnC, roomTempC, rise, gTargetAtOnC);

  gLogger.log(wallNow, LogEventType::STATE_CHANGE, Command::OFF, true);
  gHeaterWasOn = false;
}

// ── TIMEZONE FETCH ───────────────────────────────────────────
bool fetchTimezoneOffset(int32_t &outOffsetSeconds) {
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
  if (String(doc["status"].as<const char *>()) != "success") {
    Serial.println("ip-api returned non-success");
    return false;
  }
  outOffsetSeconds = doc["offset"].as<int32_t>();
  const char *tz = doc["timezone"].as<const char *>();
  Serial.printf("Detected timezone: %s (offset=%lds)\n", tz, outOffsetSeconds);
  return true;
}

// ── PORTAL CSS ───────────────────────────────────────────────
const char *portalCSS = R"(
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

// ── SETUP ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

#ifdef DEV_WIFI_SSID
  Serial.println("[WIFI] Dev mode: connecting with hardcoded credentials...");
  WiFi.begin(DEV_WIFI_SSID, DEV_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
#else
  bool reprovision = should_reprovision();
  gLogger.beginPersistence("retrofit-log");
  WiFiManager wifiManager;
  wifiManager.setCustomHeadElement(portalCSS);
  if (reprovision) {
    Serial.println("[WIFI] Reprovisioning requested, wiping credentials...");
    wifiManager.resetSettings();
  }
  wifiManager.autoConnect("ESP32-Setup");
#endif

  Serial.println();
  Serial.print("[WIFI] Connected! IP: ");
  Serial.println(WiFi.localIP());

  int32_t offsetSeconds = 0;
  if (fetchTimezoneOffset(offsetSeconds)) {
    configTime(offsetSeconds, 0, kNtpServerPrimary, kNtpServerSecondary,
               kNtpServerTertiary);
  } else {
    Serial.println("[TIME] Falling back to UTC");
    configTime(0, 0, kNtpServerPrimary, kNtpServerSecondary,
               kNtpServerTertiary);
  }

  Serial.print("[TIME] Waiting for NTP sync");
  time_t now = 0;
  while (now < 1700000000UL) {
    delay(500);
    Serial.print(".");
    time(&now);
  }
  Serial.println();

  gWallClock.setUnixTimeMs(static_cast<uint64_t>(now) * 1000ULL, millis());

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  Serial.printf("[TIME] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  gHubConnectivity.begin(gHubReceiver, gWallClock);
  gCommandScheduler.setEnabled(true);
  gPid.reset(MockRoom::roomTempC); // swap out for real sensor when ready

  Serial.printf(
      "[MOCK] Room model: start=%.1f°C outside=%.1f°C target=%.1f°C\n",
      MockRoom::kStartTempC, MockRoom::kOutsideTempC, gTargetTempC);
}

// ── LOOP ─────────────────────────────────────────────────────
void loop() {
  const uint32_t nowMs = millis();
  const uint32_t nowUs = micros();
  static uint32_t lastTelemetryMs = 0;

  // ── 1. Update mock room ───────────────────────────────────
  MockRoom::heaterOn = gHeaterPowered;
  MockRoom::update(nowMs);
  const float roomTempC = MockRoom::roomTempC;
  // ↑ Replace with real sensor when ready:
  // const float roomTempC = readTempSensor();

  // ── 2. Connectivity + time ────────────────────────────────
  gHubConnectivity.tick(nowMs, gHubReceiver, gWallClock);
  const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

  // ── 3. Apply scheduled target from hub ────────────────────
  const float scheduledTemp = gHubClient.scheduledTargetTemp();
  if (scheduledTemp > 0.0f) {
    Serial.printf("[SCHED] Target updated: %.1f°C → %.1f°C\n", gTargetTempC,
                  scheduledTemp);
    gTargetTempC = scheduledTemp;
    gHubClient.clearScheduledTargetTemp();
  }

  // ── 4. Hub tick ───────────────────────────────────────────
  gHubClient.tick(nowMs, wallNow, gHubConnectivity.wifiConnected());

  // ── 5. Adaptive tuning ────────────────────────────────────
  const AdaptiveThermostatTuning::Overrides overrides = gAdaptive.update(
      nowMs, roomTempC, gPid.mode(), gPid.baseTuningForMode(gPid.mode()));
  PidThermostatController::RuntimeOverrides rtOverrides;
  rtOverrides.enabled = true;
  rtOverrides.kp = overrides.kp;
  rtOverrides.maxSteps = overrides.maxSteps;
  gPid.setRuntimeOverrides(rtOverrides);

  // ── 6. PID tick (only when heater is on) ─────────────────
  PidThermostatController::Result pidResult{};

  if (gHeaterPowered) {
    pidResult = gPid.tick(nowMs, gTargetTempC, roomTempC);

    if (pidResult.ranControlCycle) {
      Serial.printf("[PID] room=%.2f°C target=%.2f°C err=%+.2f "
                    "P=%+.2f I=%+.3f D=%+.2f steps=%+d kp=%.2f maxSteps=%d\n",
                    roomTempC, gTargetTempC, pidResult.errorC, pidResult.p,
                    pidResult.i, pidResult.d, pidResult.steps, overrides.kp,
                    overrides.maxSteps);

      gLogger.log(
          wallNow, LogEventType::THERMOSTAT_CONTROL, Command::NONE, true,
          static_cast<uint8_t>(pidResult.steps < 0 ? 0 : pidResult.steps));

      if (pidResult.steps != 0) {
        MockRoom::applySteps(pidResult.steps, gTargetTempC);
        Serial.printf("[MOCK] heaterSetpoint now %.1f°C\n",
                      MockRoom::heaterSetpointC); // ← add this
        gAdaptive.onControlStepsSent(nowMs, roomTempC, pidResult.steps);
        // TODO: replace with real IR:
        // for (int s = 0; s < abs(pidResult.steps); s++)
        //     irSender.send(pidResult.steps > 0 ? Command::TEMP_UP :
        //     Command::TEMP_DOWN);
        Serial.printf("[IR]  → %s ×%d\n",
                      pidResult.steps > 0 ? "TEMP_UP" : "TEMP_DOWN",
                      abs(pidResult.steps));
      }
    }
  }

  // ── 7. Telemetry every 10s ────────────────────────────────
  if (nowMs - lastTelemetryMs >= 10000) {
    lastTelemetryMs = nowMs;
    HubClient::Telemetry t;
    t.roomTempC = roomTempC;
    t.targetTempC = gTargetTempC;
    t.powerOn = gHeaterPowered;
    t.pidP = pidResult.p;
    t.pidI = pidResult.i;
    t.pidD = pidResult.d;
    t.pidSteps = pidResult.steps;
    t.integral = pidResult.i;
    gHubClient.submitTelemetry(t);
  }

  // ── 8. Local scheduler ────────────────────────────────────
  Command scheduledCmd;
  if (gCommandScheduler.nextDueCommand(nowMs, wallNow, scheduledCmd)) {
    Serial.printf("[SCHED] Firing: %s\n", commandToString(scheduledCmd));
    gHubReceiver.push(scheduledCmd);
  }

  // ── 9. Execute commands ───────────────────────────────────
  Command cmd;
  while (gHubReceiver.poll(cmd)) {
    Serial.printf("[CMD] %s\n", commandToString(cmd));
    gLogger.log(wallNow, LogEventType::COMMAND_SENT, cmd, true);

    switch (cmd) {
    case Command::ON:
      if (!gHeaterPowered) {
        gHeaterPowered = true;
        MockRoom::heaterSetpointC = gTargetTempC; // ← reset mock setpoint
        gPid.reset(roomTempC);
        onHeaterTurnedOn(nowMs, wallNow, roomTempC, gTargetTempC);
      }
      break;

    case Command::OFF:
      if (gHeaterPowered) {
        gHeaterPowered = false;
        onHeaterTurnedOff(nowMs, wallNow, roomTempC);
      }
      break;

    case Command::TEMP_UP:
      if (!gHeaterPowered) {
        Serial.println("[CMD] Ignored TEMP_UP — heater is off");
        break;
      }
      gTargetTempC += 0.5f;
      Serial.printf("[CMD] Target → %.1f°C\n", gTargetTempC);
      break;

    case Command::TEMP_DOWN:
      if (!gHeaterPowered) {
        Serial.println("[CMD] Ignored TEMP_DOWN — heater is off");
        break;
      }
      gTargetTempC -= 0.5f;
      Serial.printf("[CMD] Target → %.1f°C\n", gTargetTempC);
      break;

    default:
      break;
    }
  }
}