// ============================================================
//  IoT Device Firmware
//  Set DEVICE_ID in prefferences.h; set DEVICE_PASS below before flashing
//  Dashboard: https://yourdomain.com/device/DEVICE_ID
// ============================================================

#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <HTTPClient.h>
#include <Arduino.h>
#include "prefferences.h"         // DEVICE_ID, SERVER_URL defined here


#define AP_NAME       "ESP32-Setup-" DEVICE_ID
#define POST_INTERVAL 10000   // send data every 10 seconds (ms)

unsigned long lastPost = 0;

// ── Simulated sensor reading (replace with your real sensor) ─
float readSensor() {
  // Example: replace with actual sensor code
  // e.g. return dht.readTemperature();
  // e.g. return analogRead(34) * (3.3 / 4095.0);
  return 20.0 + random(-50, 100) / 10.0;  // fake temp 15-30°C
}

// ── Send reading to your Flask server ────────────────────────
void postData(float value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Not connected, skipping post");
    return;
  }

  HTTPClient http;
  String url = String(SERVER_URL) + "/api/data/" + DEVICE_ID;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", DEVICE_PASS);  // auth via password header

  // Build JSON payload
  String payload = "{";
  payload += "\"sensor\":\"temperature\",";
  payload += "\"value\":" + String(value, 1) + ",";
  payload += "\"unit\":\"°C\"";
  payload += "}";

  Serial.print("[POST] Sending: ");
  Serial.println(payload);

  int code = http.POST(payload);

  if (code == 200) {
    Serial.println("[POST] OK");
  } else {
    Serial.print("[POST] Failed, HTTP code: ");
    Serial.println(code);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[IoT] Booting device: " DEVICE_ID);

  WiFiManager wm;

  // ── Redirect user to their dashboard after WiFi setup ──
  String dashURL = String(SERVER_URL) + "/device/" + DEVICE_ID;

  String customHead =
    "<style>"
    "body{font-family:monospace;background:#0a0a0f;color:#e2e2e8;"
    "display:flex;align-items:center;justify-content:center;"
    "min-height:100vh;margin:0}"
    ".box{text-align:center;padding:40px;border:1px solid #1e1e2e;"
    "border-radius:12px;background:#111118}"
    "a{color:#00ff88}"
    "</style>"
    "<script>"
    "var _u='" + dashURL + "';"
    "function _r(){"
      "fetch(_u,{mode:'no-cors'})"
      ".then(function(){window.location=_u})"
      ".catch(function(){setTimeout(_r,2000)})"
    "}"
    "setTimeout(_r,3000);"
    "</script>";

  String customBody =
    "<div class='box'>"
    "<p style='color:#6b6b7a;font-size:13px;margin-bottom:12px'>DEVICE // " DEVICE_ID "</p>"
    "<h2 style='margin-bottom:16px'>WiFi configured!</h2>"
    "<p style='color:#6b6b7a'>Reconnecting to your network, then redirecting to dashboard...</p>"
    "<p style='margin-top:16px'><a href='" + dashURL + "'>Open Dashboard →</a></p>"
    "</div>";

  wm.setCustomHeadElement(customHead.c_str());
  wm.setCustomMenuHTML(customBody.c_str());

  // Uncomment to force WiFi re-configuration:
  // wm.resetSettings();

  bool connected = wm.autoConnect(AP_NAME);

  if (!connected) {
    Serial.println("[WiFi] Failed to connect. Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.print("[WiFi] Connected! IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("[Dashboard] " SERVER_URL "/device/" DEVICE_ID);
}

void loop() {
  unsigned long now = millis();

  if (now - lastPost >= POST_INTERVAL) {
    lastPost = now;
    float value = readSensor();
    Serial.print("[Sensor] Reading: ");
    Serial.print(value);
    Serial.println(" °C");
    postData(value);
  }
}
