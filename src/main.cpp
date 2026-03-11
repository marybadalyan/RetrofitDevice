/*
  esp32_hub_client.ino
  
  Drop this alongside your existing PID / heater / IR files.
  
  First boot:
    1. ESP32 opens WiFi hotspot "ThermoSetup"
    2. You connect phone/PC to that network
    3. Visit http://192.168.4.1 to enter your WiFi + hub IP
    4. ESP32 saves to flash, reboots, connects to your network
  
  Normal operation every controlIntervalMs:
    1. Read sensor → run PID → send IR commands
    2. POST /api/telemetry to hub
    3. GET  /api/command/pending → execute any queued command
    4. Every 6 hours: GET /api/schedule to refresh schedule
    5. Every boot:    GET /api/config/esp32 to pull latest config
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ── Your existing headers ──────────────────────────────────
// Uncomment these when integrating with your actual codebase:
// #include "pid_thermostat_controller.h"
// #include "IRSender.h"
// #include "IRReciever.h"
// #include "heater.h"

// ── DEFAULTS (overridden by saved config) ──────────────────
struct DeviceConfig {
    String  wifiSsid       = "";
    String  wifiPassword   = "";
    String  hubIp          = "192.168.1.100";
    int     hubPort        = 5000;
    int     irTxPin        = 4;
    int     irRxPin        = 15;
    int     ledRedPin      = 25;
    int     ledGreenPin    = 26;
    int     ledBluePin     = 27;
    String  pidMode        = "FAST";
    float   pidKp          = 1.6f;
    float   pidKi          = 0.02f;
    float   pidKd          = 3.0f;
    int     pidMaxSteps    = 3;
    int     controlIntervalS = 45;
    float   deadbandC      = 0.3f;
};

// ── SCHEDULE ENTRY ─────────────────────────────────────────
struct ScheduleEntry {
    String day;    // "Mon" .. "Sun"
    String time;   // "HH:MM"
    float  temp;
};

// ── GLOBALS ────────────────────────────────────────────────
DeviceConfig cfg;
Preferences  prefs;
WebServer    portalServer(80);

bool          wifiConnected      = false;
bool          firstBoot          = false;
unsigned long lastTelemetryMs    = 0;
unsigned long lastScheduleMs     = 0;
const long    SCHEDULE_INTERVAL  = 6UL * 3600UL * 1000UL;  // 6 hours

// Simulated sensor state (replace with your actual sensor reads)
float   roomTempC    = 20.0f;
float   targetTempC  = 21.0f;
bool    heaterOn     = false;
float   pidIntegral  = 0.0f;
float   lastPidP     = 0.0f, lastPidI = 0.0f, lastPidD = 0.0f;
int8_t  lastSteps    = 0;

ScheduleEntry schedule[64];
int           scheduleCount = 0;

// ─────────────────────────────────────────────────────────────
//  PREFERENCES: LOAD / SAVE
// ─────────────────────────────────────────────────────────────
void loadConfig() {
    prefs.begin("thermo", true);
    cfg.wifiSsid        = prefs.getString("ssid",      "");
    cfg.wifiPassword    = prefs.getString("pass",      "");
    cfg.hubIp           = prefs.getString("hubIp",     "192.168.1.100");
    cfg.hubPort         = prefs.getInt   ("hubPort",   5000);
    cfg.irTxPin         = prefs.getInt   ("irTx",      4);
    cfg.irRxPin         = prefs.getInt   ("irRx",      15);
    cfg.ledRedPin       = prefs.getInt   ("ledR",      25);
    cfg.ledGreenPin     = prefs.getInt   ("ledG",      26);
    cfg.ledBluePin      = prefs.getInt   ("ledB",      27);
    cfg.pidMode         = prefs.getString("pidMode",   "FAST");
    cfg.pidKp           = prefs.getFloat ("pidKp",     1.6f);
    cfg.pidKi           = prefs.getFloat ("pidKi",     0.02f);
    cfg.pidKd           = prefs.getFloat ("pidKd",     3.0f);
    cfg.pidMaxSteps     = prefs.getInt   ("pidMs",     3);
    cfg.controlIntervalS= prefs.getInt   ("ctrlInt",   45);
    cfg.deadbandC       = prefs.getFloat ("deadband",  0.3f);
    prefs.end();
}

void saveConfig() {
    prefs.begin("thermo", false);
    prefs.putString("ssid",    cfg.wifiSsid);
    prefs.putString("pass",    cfg.wifiPassword);
    prefs.putString("hubIp",   cfg.hubIp);
    prefs.putInt   ("hubPort", cfg.hubPort);
    prefs.putInt   ("irTx",    cfg.irTxPin);
    prefs.putInt   ("irRx",    cfg.irRxPin);
    prefs.putInt   ("ledR",    cfg.ledRedPin);
    prefs.putInt   ("ledG",    cfg.ledGreenPin);
    prefs.putInt   ("ledB",    cfg.ledBluePin);
    prefs.putString("pidMode", cfg.pidMode);
    prefs.putFloat ("pidKp",   cfg.pidKp);
    prefs.putFloat ("pidKi",   cfg.pidKi);
    prefs.putFloat ("pidKd",   cfg.pidKd);
    prefs.putInt   ("pidMs",   cfg.pidMaxSteps);
    prefs.putInt   ("ctrlInt", cfg.controlIntervalS);
    prefs.putFloat ("deadband",cfg.deadbandC);
    prefs.end();
    Serial.println("[CFG] Saved to flash.");
}

// ─────────────────────────────────────────────────────────────
//  FIRST-BOOT CAPTIVE PORTAL
// ─────────────────────────────────────────────────────────────
const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ThermoHub Setup</title>
<style>
  body{font-family:monospace;background:#f5f0eb;color:#2a1f14;padding:24px;max-width:440px;margin:0 auto;}
  h1{font-size:1.6rem;margin-bottom:4px;}
  h1 span{color:#c45c1a;font-style:italic;}
  p{color:#9c8b7a;font-size:.85rem;margin-bottom:24px;}
  .section{background:#fff;border:1px solid #e8e0d6;border-radius:12px;padding:18px;margin-bottom:16px;}
  .section h2{font-size:.85rem;letter-spacing:2px;text-transform:uppercase;color:#9c8b7a;margin-bottom:14px;}
  label{display:block;font-size:.72rem;letter-spacing:1.5px;text-transform:uppercase;color:#9c8b7a;margin-bottom:4px;}
  input{width:100%;background:#faf7f4;border:1px solid #e8e0d6;border-radius:8px;padding:9px 12px;font-family:monospace;font-size:.82rem;color:#2a1f14;box-sizing:border-box;margin-bottom:12px;outline:none;}
  input:focus{border-color:#c45c1a;}
  button{width:100%;background:#c45c1a;color:#fff;border:none;border-radius:10px;padding:12px;font-family:monospace;font-size:.8rem;letter-spacing:1.5px;text-transform:uppercase;cursor:pointer;margin-top:4px;}
  button:hover{background:#a84a12;}
</style>
</head><body>
<h1>thermo<span>hub</span> setup</h1>
<p>Connect to your WiFi and configure hub address. Settings are saved to flash.</p>
<form method="POST" action="/save">
  <div class="section">
    <h2>WiFi</h2>
    <label>SSID (network name)</label>
    <input type="text" name="ssid" placeholder="MyNetwork" required>
    <label>Password</label>
    <input type="password" name="pass" placeholder="••••••••">
  </div>
  <div class="section">
    <h2>Hub</h2>
    <label>Hub IP address (your PC/Pi)</label>
    <input type="text" name="hubIp" placeholder="192.168.1.100" value="192.168.1.100">
    <label>Hub Port</label>
    <input type="number" name="hubPort" value="5000">
  </div>
  <div class="section">
    <h2>Pins</h2>
    <label>IR Transmit Pin</label>
    <input type="number" name="irTx" value="4">
    <label>IR Receive Pin</label>
    <input type="number" name="irRx" value="15">
    <label>LED Red Pin</label>
    <input type="number" name="ledR" value="25">
    <label>LED Green Pin</label>
    <input type="number" name="ledG" value="26">
    <label>LED Blue Pin</label>
    <input type="number" name="ledB" value="27">
  </div>
  <button type="submit">Save &amp; Connect</button>
</form>
</body></html>
)rawhtml";

void startPortal() {
    Serial.println("[PORTAL] Starting setup hotspot: ThermoSetup");
    WiFi.softAP("ThermoSetup", "thermosetup");
    Serial.print("[PORTAL] IP: ");
    Serial.println(WiFi.softAPIP());

    portalServer.on("/", HTTP_GET, []() {
        portalServer.send(200, "text/html", PORTAL_HTML);
    });

    portalServer.on("/save", HTTP_POST, []() {
        if (portalServer.hasArg("ssid"))    cfg.wifiSsid     = portalServer.arg("ssid");
        if (portalServer.hasArg("pass"))    cfg.wifiPassword = portalServer.arg("pass");
        if (portalServer.hasArg("hubIp"))   cfg.hubIp        = portalServer.arg("hubIp");
        if (portalServer.hasArg("hubPort")) cfg.hubPort      = portalServer.arg("hubPort").toInt();
        if (portalServer.hasArg("irTx"))    cfg.irTxPin      = portalServer.arg("irTx").toInt();
        if (portalServer.hasArg("irRx"))    cfg.irRxPin      = portalServer.arg("irRx").toInt();
        if (portalServer.hasArg("ledR"))    cfg.ledRedPin    = portalServer.arg("ledR").toInt();
        if (portalServer.hasArg("ledG"))    cfg.ledGreenPin  = portalServer.arg("ledG").toInt();
        if (portalServer.hasArg("ledB"))    cfg.ledBluePin   = portalServer.arg("ledB").toInt();
        saveConfig();
        portalServer.send(200, "text/html",
            "<html><body style='font-family:monospace;padding:24px;background:#f5f0eb'>"
            "<h2 style='color:#4a8c5c'>✓ Saved! Rebooting…</h2>"
            "<p>The device will now connect to your WiFi. "
            "Visit <strong>http://"+cfg.hubIp+":"+String(cfg.hubPort)+"</strong> for the dashboard.</p>"
            "</body></html>");
        delay(2000);
        ESP.restart();
    });

    portalServer.begin();
    firstBoot = true;
}

// ─────────────────────────────────────────────────────────────
//  WIFI CONNECT
// ─────────────────────────────────────────────────────────────
bool connectWifi() {
    if (cfg.wifiSsid.isEmpty()) return false;
    Serial.printf("[WiFi] Connecting to %s", cfg.wifiSsid.c_str());
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.isConnected()) {
        Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    Serial.println("\n[WiFi] Failed.");
    return false;
}

// ─────────────────────────────────────────────────────────────
//  HUB: PULL REMOTE CONFIG (called on boot)
// ─────────────────────────────────────────────────────────────
void pullRemoteConfig() {
    HTTPClient http;
    String url = "http://" + cfg.hubIp + ":" + cfg.hubPort + "/api/config/esp32";
    http.begin(url);
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) { http.end(); return; }
    http.end();

    bool changed = false;
    if (doc.containsKey("pid_kp"))           { cfg.pidKp          = doc["pid_kp"];          changed = true; }
    if (doc.containsKey("pid_ki"))           { cfg.pidKi          = doc["pid_ki"];          changed = true; }
    if (doc.containsKey("pid_kd"))           { cfg.pidKd          = doc["pid_kd"];          changed = true; }
    if (doc.containsKey("pid_max_steps"))    { cfg.pidMaxSteps    = doc["pid_max_steps"];   changed = true; }
    if (doc.containsKey("pid_mode"))         { cfg.pidMode        = (const char*)doc["pid_mode"]; changed = true; }
    if (doc.containsKey("control_interval_s")){ cfg.controlIntervalS = doc["control_interval_s"]; changed = true; }
    if (doc.containsKey("deadband_c"))       { cfg.deadbandC      = doc["deadband_c"];      changed = true; }

    if (changed) {
        saveConfig();
        Serial.println("[HUB] Remote config applied.");
    }
}

// ─────────────────────────────────────────────────────────────
//  HUB: POST TELEMETRY
// ─────────────────────────────────────────────────────────────
void postTelemetry() {
    if (!WiFi.isConnected()) return;

    StaticJsonDocument<256> doc;
    doc["room_temp"]   = roomTempC;
    doc["target_temp"] = targetTempC;
    doc["power"]       = heaterOn;
    doc["mode"]        = cfg.pidMode;
    doc["pid_p"]       = lastPidP;
    doc["pid_i"]       = lastPidI;
    doc["pid_d"]       = lastPidD;
    doc["pid_steps"]   = lastSteps;
    doc["integral"]    = pidIntegral;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin("http://" + cfg.hubIp + ":" + cfg.hubPort + "/api/telemetry");
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);

    if (code == 200) {
        // Hub may respond with a schedule override
        StaticJsonDocument<128> resp;
        if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
            if (resp.containsKey("scheduled_target")) {
                float st = resp["scheduled_target"];
                Serial.printf("[HUB] Schedule override: target → %.1f°C\n", st);
                targetTempC = st;
                // TODO: nudge your PID setpoint here
            }
        }
    }
    http.end();
}

// ─────────────────────────────────────────────────────────────
//  HUB: POLL PENDING COMMAND
// ─────────────────────────────────────────────────────────────
void pollPendingCommand() {
    if (!WiFi.isConnected()) return;

    HTTPClient http;
    http.begin("http://" + cfg.hubIp + ":" + cfg.hubPort + "/api/command/pending");
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) { http.end(); return; }
    http.end();

    const char* cmd = doc["command"] | "";
    if (strlen(cmd) == 0) return;

    Serial.printf("[CMD] Executing: %s\n", cmd);

    if      (strcmp(cmd, "on")        == 0) { heaterOn = true;  /* IRSender: send ON */ }
    else if (strcmp(cmd, "off")       == 0) { heaterOn = false; /* IRSender: send OFF */ }
    else if (strcmp(cmd, "temp_up")   == 0) { /* IRSender: send TEMP_UP */ }
    else if (strcmp(cmd, "temp_down") == 0) { /* IRSender: send TEMP_DOWN */ }
}

// ─────────────────────────────────────────────────────────────
//  HUB: PULL SCHEDULE
// ─────────────────────────────────────────────────────────────
void pullSchedule() {
    if (!WiFi.isConnected()) return;

    HTTPClient http;
    http.begin("http://" + cfg.hubIp + ":" + cfg.hubPort + "/api/schedule");
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    StaticJsonDocument<2048> doc;
    if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) { http.end(); return; }
    http.end();

    JsonArray arr = doc["schedule"].as<JsonArray>();
    scheduleCount = 0;
    for (JsonObject entry : arr) {
        if (scheduleCount >= 64) break;
        schedule[scheduleCount].day  = (const char*)entry["day"];
        schedule[scheduleCount].time = (const char*)entry["time"];
        schedule[scheduleCount].temp = entry["temp"].as<float>();
        scheduleCount++;
    }
    Serial.printf("[SCHED] Loaded %d entries.\n", scheduleCount);
}

// ─────────────────────────────────────────────────────────────
//  SCHEDULE: GET CURRENT TARGET TEMP
// ─────────────────────────────────────────────────────────────
// Helper: day abbreviation from tm struct
const char* dowStr(int wday) {
    const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    return days[wday % 7];
}

float getScheduledTemp() {
    // Requires NTP time — uncomment configTime in setup() below.
    // Returns -1 if no schedule entry found.
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    const char* today = dowStr(t->tm_wday);
    char nowTime[6];
    snprintf(nowTime, sizeof(nowTime), "%02d:%02d", t->tm_hour, t->tm_min);

    // Find latest entry for today that is <= current time
    float best = -1.0f;
    String bestTime = "";
    for (int i = 0; i < scheduleCount; i++) {
        if (schedule[i].day == today && schedule[i].time <= String(nowTime)) {
            if (bestTime.isEmpty() || schedule[i].time > bestTime) {
                bestTime = schedule[i].time;
                best     = schedule[i].temp;
            }
        }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[BOOT] ThermoHub ESP32 client");

    loadConfig();

    // First boot detection: no SSID saved
    if (cfg.wifiSsid.isEmpty()) {
        startPortal();
        return;
    }

    wifiConnected = connectWifi();

    if (!wifiConnected) {
        Serial.println("[BOOT] WiFi failed — starting portal for reconfiguration.");
        startPortal();
        return;
    }

    // Sync time via NTP (needed for schedule)
    configTime(0, 0, "pool.ntp.org");
    Serial.print("[NTP] Syncing time");
    time_t t = time(nullptr);
    for (int i = 0; i < 10 && t < 1000; i++) { delay(500); t = time(nullptr); Serial.print("."); }
    Serial.println(" done.");

    // Pull latest config and schedule from hub
    pullRemoteConfig();
    pullSchedule();

    // Apply hardware config
    // IRSender.begin();
    // IRReceiver.begin();
    // etc.

    Serial.println("[BOOT] Ready.");
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
    // If in portal mode, serve web requests
    if (firstBoot) {
        portalServer.handleClient();
        return;
    }

    if (!WiFi.isConnected()) connectWifi();

    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)cfg.controlIntervalS * 1000UL;

    if (now - lastTelemetryMs >= intervalMs) {
        lastTelemetryMs = now;

        // ── 1. Read your actual sensor here ──────────────────
        // roomTempC = readTempSensor();

        // ── 2. Check schedule for target override ────────────
        float schedTemp = getScheduledTemp();
        if (schedTemp > 0) targetTempC = schedTemp;

        // ── 3. Run your PID controller ───────────────────────
        // PidThermostatController::Result result = pidController.tick(now, targetTempC, roomTempC);
        // lastPidP = result.p; lastPidI = result.i; lastPidD = result.d; lastSteps = result.steps;
        // pidIntegral = /* pidController.integral() */;
        // for (int s = 0; s < abs(result.steps); s++)
        //     irSender.sendCommand(result.steps > 0 ? Command::TEMP_UP : Command::TEMP_DOWN);

        // ── 4. Send telemetry to hub ──────────────────────────
        postTelemetry();

        // ── 5. Execute any pending hub command ───────────────
        pollPendingCommand();
    }

    // Refresh schedule every 6 hours
    if (now - lastScheduleMs >= SCHEDULE_INTERVAL) {
        lastScheduleMs = now;
        pullSchedule();
        pullRemoteConfig();
    }

    delay(50);
}
