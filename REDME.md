

**`dashboard.html`** — open this in a browser (or it's served automatically at `http://your-pi:5000`). Four tabs matching your Blynk layout: Control (On/Off toggle + Up/Down stepper + live chart exactly like your screenshot), Schedule (weekly grid + manual entry + auto-generate from history), History (24h chart + stats + heater calibration info), and Config (all pin numbers + PID sliders).

**`thermohub.py`** — run with `uvicorn thermohub:app --host 0.0.0.0 --port 5000`. Handles everything: stores all telemetry in SQLite, serves the dashboard, pushes schedule overrides back to the ESP32, and the auto-generate endpoint analyses your usage history to build a schedule automatically. Run it with just `pip install fastapi uvicorn scikit-learn pandas numpy`.

**`esp32_hub_client.ino`** — drop this alongside your existing PID/IR files. First boot with no saved WiFi → opens a `ThermoSetup` hotspot, you visit `192.168.4.1`, fill in your WiFi + hub IP + pin numbers, it saves to flash and reboots. After that it POSTs telemetry every 45s, polls for commands, and pulls the schedule every 6 hours. The integration points are clearly marked with `// TODO` comments where you wire in your existing `PidThermostatController`, `IRSender`, and sensor reads.

ThermoHub
ESP32 Integration Guide
How your code connects to the dashboard — pins, widgets, commands, schedule

1.  How This System Works
In Blynk you "connect a pin to a widget" and it just appears. In ThermoHub the idea is the same — but instead of Blynk's cloud doing the wiring, your ESP32 sends a small JSON packet to the hub over HTTP every 45 seconds, and the hub drives everything on the dashboard. There is no magic pin numbering to memorise.
The whole flow in one sentence:
ESP32  →  POST /api/telemetry  →  Hub stores + updates dashboard  →  User sees live data
And in reverse:
User presses button  →  Hub queues command  →  ESP32 polls /api/command/pending  →  IRSender fires

2.  Your Existing Classes
Here is every class you have already written and exactly what it does. You do not need to change any of them.
2.1  IRSender
Member
What it does
begin()
Calls ledcSetup + ledcAttachPin using kIrTxPin from prefferences.h
sendCommand(Command)
Validates command, encodes NEC frame, fires IR pulses. Returns TxFailureCode.
TxFailureCode enum
NONE / NOT_INITIALIZED / INVALID_COMMAND / INVALID_CONFIG / HW_UNAVAILABLE

2.2  IRReceiver
Member
What it does
begin()
Attaches hardware interrupt on kIrRxPin, records pulse durations in IRAM
poll(DecodedFrame&)
Copies pulses out of ISR buffer, decodes NEC frame, returns true if valid Command received
onEdgeInterrupt()
ISR — do NOT call this yourself, hardware calls it automatically

IRReceiver uses a hardware interrupt. Never call HTTP or delay() while the ISR could be running — always do your HTTP calls after poll() in the main loop, not inside an interrupt handler.
2.3  Heater
Member
What it does
applyCommand(Command)
Sets powerEnabled_ to true/false on ON/OFF, returns true on success
powerEnabled()
Returns current tracked power state (bool)

2.4  CommandStatusLed
Command
LED colour
ON
Green
OFF
Red
TEMP_UP
Blue
TEMP_DOWN
Yellow

2.5  Logger
Your Logger class stores up to 128 LogEntry records in RAM and can persist them via Preferences. Each entry captures uptime, Unix time, LogEventType, Command, success flag, and a detail code. The hub uses this data to calibrate the heater warm-up estimate in the History tab.
LogEventType
When to use it
COMMAND_SENT
After IRSender.sendCommand() returns NONE
COMMAND_DROPPED
When PID output is non-zero but deadband suppresses the command
HUB_COMMAND_RX
After polling /api/command/pending and receiving a non-null command
SCHEDULE_COMMAND
When target temp changes due to schedule override from hub
STATE_CHANGE
Heater power ON/OFF transitions
THERMOSTAT_CONTROL
Each time PidThermostatController::tick() runs
TRANSMIT_FAILED
When IRSender returns anything other than TxFailureCode::NONE
IR_FRAME_RX
When IRReceiver::poll() returns true (remote pressed)

3.  prefferences.h — Your Pin File
This is the only file you edit to change hardware wiring. Every other file reads from it via constants. Think of it as your "pin map".
A minimal prefferences.h looks like this:
#pragma once
#include <cstdint>

// ── IR ────────────────────────────────────────
constexpr int     kIrTxPin            = 4;   // GPIO connected to IR LED
constexpr int     kIrRxPin            = 15;  // GPIO connected to IR receiver
constexpr int     kIrPwmChannel       = 0;   // ESP32 LEDC channel (0–15)
constexpr uint32_t kIrCarrierFreqHz   = 38000U; // 38 kHz NEC standard
constexpr uint8_t kIrPwmResolutionBits = 8U;

// ── NEC protocol address + command bytes ──────
constexpr uint16_t kNecDeviceAddress  = 0x00FF; // read from your heater remote
constexpr uint8_t  kNecCommandOn      = 0x45;   // read from your heater remote
constexpr uint8_t  kNecCommandOff     = 0x46;
constexpr uint8_t  kNecCommandTempUp  = 0x47;
constexpr uint8_t  kNecCommandTempDown= 0x44;

// ── Status LED (RGB) ──────────────────────────
constexpr bool kStatusLedEnabled      = true;
constexpr int  kStatusLedRedPin       = 25;
constexpr int  kStatusLedGreenPin     = 26;
constexpr int  kStatusLedBluePin      = 27;

// ── Diagnostics ───────────────────────────────
constexpr uint8_t kDiagnosticsLogLevel = 1U; // 0=off 1=errors 2=verbose
The Config tab on the dashboard lets you update these values at runtime. When you press "Save to ESP32", the hub stores them in SQLite and the ESP32 picks them up on the next boot via GET /api/config/esp32. You still need this file to compile, but its defaults can be overridden without reflashing.

4.  Dashboard Widgets — What Talks to What
This is the equivalent of Blynk's "pin-to-widget" mapping. Instead of virtual pins V0, V1… each piece of data has a name in the JSON.
4.1  On/Off Toggle
Dashboard button → POST /api/command → body: {"command":"on"} or {"command":"off"}.
Your ESP32 polls GET /api/command/pending after each telemetry cycle. When it receives "on" or "off", you call:
irSender.sendCommand(Command::ON);
heater.applyCommand(Command::ON);
cmdLed.showCommand(Command::ON);
logger.log(snap, LogEventType::HUB_COMMAND_RX, Command::ON, true);
4.2  Up/Down Stepper
Each press of + or − sends {"command":"temp_up"} or {"command":"temp_down"} to the hub's command queue. On the ESP32 side:
irSender.sendCommand(Command::TEMP_UP);
cmdLed.showCommand(Command::TEMP_UP);
logger.log(snap, LogEventType::HUB_COMMAND_RX, Command::TEMP_UP, true);
4.3  Room Temperature Gauge
The gauge reads from the room_temp field in your telemetry POST. Every 45 seconds your ESP32 sends:
// In your main loop, after PID tick:
http.begin("http://" + cfg.hubIp + ":5000/api/telemetry");
http.addHeader("Content-Type", "application/json");
http.POST("{\"room_temp\":" + String(roomTempC, 1)
        + ",\"target_temp\":" + String(targetTempC, 1)
        + ",\"power\":"     + (heater.powerEnabled() ? "true" : "false")
        + ",\"mode\":\""  + cfg.pidMode + "\"
        + ",\"pid_p\":"    + String(result.p, 2)
        + ",\"pid_i\":"    + String(result.i, 3)
        + ",\"pid_d\":"    + String(result.d, 2)
        + ",\"pid_steps\":"+ String(result.steps)
        + ",\"integral\":" + String(pidController.integral(), 3)
        + "}");
4.4  "Heat over time" Chart
The chart plots room_temp and target_temp from your telemetry stream. No extra code needed — every POST you make automatically feeds it.
4.5  PID readout (P / I / D / Steps)
The small numbers under the temperature come from pid_p, pid_i, pid_d, pid_steps in your telemetry JSON. They map directly to the fields in PidThermostatController::Result.
4.6  Mode badge (FAST / ECO)
Set the mode field in your telemetry to "FAST" or "ECO" to show the correct badge. It just reads the string — no special value.
4.7  Schedule → Target Temperature
When the schedule says the temperature should change, the hub responds to your telemetry POST with an extra field:
// Hub response JSON when schedule override is active:
{ "status": "ok", "scheduled_target": 21.5 }

// Check for it after your POST:
StaticJsonDocument<128> resp;
deserializeJson(resp, http.getString());
if (resp.containsKey("scheduled_target")) {
    targetTempC = resp["scheduled_target"];
    pidController.reset(roomTempC); // optional: reset integral on big jumps
    logger.log(snap, LogEventType::SCHEDULE_COMMAND, Command::NONE, true);
}

5.  Complete Main Loop Integration
This is what your main .ino file looks like when all your classes are wired together. Each numbered comment corresponds to a step.
#include "IRSender.h"
#include "IRReciever.h"
#include "heater.h"
#include "pid_thermostat_controller.h"
#include "logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

IRSender    irSender;
IRReceiver  irReceiver;
Heater      heater;
CommandStatusLed cmdLed;
DisplayDriver    display;
Logger      logger;
PidThermostatController pid;

float roomTempC   = 20.0f;
float targetTempC = 21.0f;
unsigned long lastCycleMs = 0;

void setup() {
    Serial.begin(115200);
    irSender.begin();     // uses kIrTxPin from prefferences.h
    irReceiver.begin();   // uses kIrRxPin, attaches ISR
    cmdLed.begin();       // uses kStatusLed*Pin
    display.begin();
    logger.beginPersistence("thermoLog");
    pid.setMode(ThermostatMode::FAST);
    // WiFi + NTP setup (see esp32_hub_client.ino)
    connectWifi();
    pullRemoteConfig();   // GET /api/config/esp32
    pullSchedule();       // GET /api/schedule
}

void loop() {
    const uint32_t nowMs = millis();
    const uint32_t intervalMs = cfg.controlIntervalS * 1000UL;

    // ── STEP 1: Check physical remote ─────────────
    // IRReceiver interrupt fires in background.
    // poll() safely copies out of ISR buffer.
    DecodedFrame frame;
    if (irReceiver.poll(frame)) {
        heater.applyCommand(frame.command);
        cmdLed.showCommand(frame.command);
        display.showPowerState(heater.powerEnabled());
        logger.log(snap, LogEventType::IR_FRAME_RX, frame.command, true);
    }

    // ── STEP 2: PID control cycle ──────────────────
    if (nowMs - lastCycleMs >= intervalMs) {
        lastCycleMs = nowMs;
        roomTempC = readTempSensor(); // your sensor read here

        auto result = pid.tick(nowMs, targetTempC, roomTempC);
        if (result.ranControlCycle) {
            logger.log(snap, LogEventType::THERMOSTAT_CONTROL,
                       Command::NONE, true, (uint8_t)result.steps);

            // Send IR steps to heater
            for (int8_t s = 0; s < abs(result.steps); s++) {
                Command cmd = result.steps > 0 ? Command::TEMP_UP : Command::TEMP_DOWN;
                TxFailureCode r = irSender.sendCommand(cmd);
                cmdLed.showCommand(cmd);
                if (r == TxFailureCode::NONE) {
                    logger.log(snap, LogEventType::COMMAND_SENT, cmd, true);
                } else {
                    logger.log(snap, LogEventType::TRANSMIT_FAILED, cmd, false, (uint8_t)r);
                }
                delay(50); // brief gap between repeated IR pulses
            }
        }

        // ── STEP 3: Send telemetry to hub ──────────
        postTelemetry(roomTempC, targetTempC, result);
        // (postTelemetry also checks for scheduled_target in response)

        // ── STEP 4: Execute any hub command ─────────
        pollPendingCommand(); // see esp32_hub_client.ino
    }
}

6.  First Boot — Configuring Pins Without Reflashing
The first time the ESP32 boots with no saved WiFi, it opens a hotspot called ThermoSetup. Connect your phone or PC to it and navigate to:
http://192.168.4.1
You will see a setup page with fields for:
	•	WiFi network name and password
	•	Hub IP address and port (e.g. 192.168.1.100 : 5000)
	•	IR Transmit Pin (the GPIO your IR LED is wired to)
	•	IR Receive Pin (the GPIO your IR receiver is wired to)
	•	LED Red, Green, Blue pins

After you press Save, the values are written to ESP32 flash with Preferences, the device reboots, and connects to your WiFi. You never need to edit prefferences.h or reflash just to change a pin.
If WiFi fails after setup, the device falls back to the portal again automatically so you can correct the credentials.
After first boot, all pin changes can also be made from the Config tab in the dashboard. Press "Save to ESP32" — the hub stores the new values, and the ESP32 pulls them on next reboot via GET /api/config/esp32.

7.  How the Schedule Works
The Schedule tab lets you define a weekly timetable: for each day and time you set a target temperature. When the ESP32's telemetry POST arrives, the hub checks what the schedule says right now and returns a scheduled_target in the response if the target needs to change.
7.1  Setting it manually
	•	Open the Schedule tab in the dashboard
	•	Click "+ Add slot" for each rule
	•	Choose day, time, and target temperature (e.g. Mon 07:00 → 21°C)
	•	The weekly overview grid updates live as you add entries
	•	Press "Save schedule" — this POSTs to /api/schedule and stores it in SQLite
7.2  Auto-generating from history
Once the ESP32 has sent enough telemetry (the hub needs ~50 powered-on readings), press the "Auto-generate from history" button. The hub analyses which times of day you typically had the heater on and at what temperature, then proposes a schedule. You can edit it before saving.
7.3  Pre-heat lead time
The History tab shows a Heater Calibration section that estimates your heater's warm-up time from the PID integral history. A slow heater (large accumulated integral) means the hub will eventually recommend starting earlier. This is displayed as information — implementing automatic lead-time offsets is the next planned feature.

8.  Full API Reference
All endpoints are on your hub machine at port 5000. The ESP32 calls the first three; the dashboard calls the rest.
Endpoint
What it does
POST /api/telemetry
ESP32 sends sensor data every 45s. Hub stores in SQLite and may return scheduled_target.
GET  /api/command/pending
ESP32 polls after each telemetry. Returns next queued command or null.
GET  /api/config/esp32
ESP32 calls on boot. Returns saved pin + PID config.
GET  /api/status
Dashboard polls every 5s. Returns live device state.
POST /api/command
Dashboard sends on button press. Queues: on/off/temp_up/temp_down.
GET  /api/history?hours=24
Dashboard history tab. Returns readings array + calibration estimate.
GET  /api/schedule
Dashboard and ESP32 fetch current weekly schedule.
POST /api/schedule
Dashboard saves edited schedule entries.
POST /api/schedule/generate
Dashboard triggers auto-generation from history.
GET  /api/config
Dashboard config tab loads current saved config.
POST /api/config
Dashboard saves config changes (pins, PID, WiFi).

9.  Files Checklist
File
Purpose
prefferences.h
Your pin map. Edit to set GPIO numbers and NEC bytes.
commands.h
Command enum: ON / OFF / TEMP_UP / TEMP_DOWN / NONE
IRSender.h / .cpp
Sends NEC IR frames via LEDC PWM — do not modify
IRReciever.h / .cpp
Receives NEC IR via hardware interrupt — do not modify
heater.h / .cpp
Tracks power state, drives status LED
protocol.h / .cpp
NEC packet encode/decode — do not modify
logger.h
128-entry ring buffer with Preferences persistence
pid_thermostat_controller.h/.cpp
PID control loop — runs entirely on ESP32
esp32_hub_client.ino
Main sketch: WiFi, first-boot portal, HTTP calls
thermohub.py
Hub server: FastAPI + SQLite + schedule ML
dashboard.html
Web UI served at http://your-hub-ip:5000
