#include "SetupPortal.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if __has_include(<WiFi.h>)
#include <WiFi.h>
#endif

static constexpr const char* kApSsid = "ThermoSetup";

// ── Minimal HTML page — styled to match the dashboard ────────────────────────
static const char kPortalHtml[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ThermoHub Setup</title>
<style>
  :root{--bg:#f5f0eb;--card:#fff;--border:#e8e0d6;--accent:#c45c1a;--text:#2a1f14;--muted:#9c8b7a;}
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:var(--bg);font-family:'DM Mono',monospace,sans-serif;
       min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
  .card{background:var(--card);border:1px solid var(--border);border-radius:20px;
        padding:36px;width:100%;max-width:420px;}
  .logo{font-size:1.6rem;margin-bottom:4px;}
  .logo span{color:var(--accent);font-style:italic;}
  .sub{font-size:0.7rem;letter-spacing:2px;text-transform:uppercase;
       color:var(--muted);margin-bottom:28px;}
  label{display:block;font-size:0.62rem;letter-spacing:2px;text-transform:uppercase;
        color:var(--muted);margin-bottom:5px;margin-top:14px;}
  input{width:100%;background:var(--bg);border:1px solid var(--border);border-radius:9px;
        padding:10px 13px;font-family:inherit;font-size:0.85rem;color:var(--text);
        outline:none;transition:border-color .2s;}
  input:focus{border-color:var(--accent);}
  .row{display:grid;grid-template-columns:1fr 100px;gap:10px;}
  button{margin-top:24px;width:100%;padding:12px;background:var(--accent);
         border:none;border-radius:10px;color:#fff;font-family:inherit;
         font-size:0.75rem;letter-spacing:1.5px;text-transform:uppercase;
         cursor:pointer;transition:background .2s;}
  button:hover{background:#a84a12;}
  .note{font-size:0.65rem;color:var(--muted);margin-top:14px;line-height:1.6;text-align:center;}
</style></head><body>
<div class="card">
  <div class="logo">thermo<span>hub</span></div>
  <div class="sub">First-time setup</div>
  <form method="POST" action="/save">
    <label>WiFi Network (SSID)</label>
    <input name="ssid" type="text" placeholder="MyNetwork" required autocomplete="off">
    <label>WiFi Password</label>
    <input name="pass" type="password" placeholder="••••••••" autocomplete="off">
    <label>Hub IP Address</label>
    <div class="row">
      <input name="host" type="text" placeholder="192.168.1.x" required>
      <input name="port" type="number" placeholder="5000" value="5000">
    </div>
    <button type="submit">Save &amp; Connect</button>
  </form>
  <div class="note">
    After saving, the device will reboot and connect to your network.<br>
    The hub IP is your Mac/Pi running thermohub.py.
  </div>
</div>
</body></html>
)rawhtml";

static const char kSavedHtml[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>
  body{background:#f5f0eb;font-family:monospace;display:flex;align-items:center;
       justify-content:center;min-height:100vh;color:#2a1f14;}
  .card{background:#fff;border:1px solid #e8e0d6;border-radius:20px;padding:36px;
        text-align:center;max-width:360px;}
  h2{color:#c45c1a;margin-bottom:10px;}
  p{font-size:0.8rem;color:#9c8b7a;line-height:1.7;}
</style></head><body>
<div class="card">
  <h2>✓ Saved</h2>
  <p>Config saved to flash.<br>Device is rebooting and will connect to your network.</p>
</div>
</body></html>
)rawhtml";

// ── Implementation ────────────────────────────────────────────────────────────

SetupPortal::SetupPortal(DeviceConfig& cfg) : cfg_(cfg) {}

void SetupPortal::begin() {
#if PORTAL_HAS_WEBSERVER && __has_include(<WiFi.h>)
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApSsid);

#if __has_include(<Arduino.h>)
    Serial.print("[PORTAL] AP started: ");
    Serial.println(kApSsid);
    Serial.println("[PORTAL] Connect and open http://192.168.4.1");
#endif

    server_.on("/",     [this]() { handleRoot(); });
    server_.on("/save", HTTP_POST, [this]() { handleSave(); });
    server_.onNotFound( [this]() { handleRoot(); });
    server_.begin();
#endif
}

void SetupPortal::tick() {
#if PORTAL_HAS_WEBSERVER
    server_.handleClient();
#endif
}

#if PORTAL_HAS_WEBSERVER
void SetupPortal::handleRoot() {
    server_.send_P(200, "text/html", kPortalHtml);
}

void SetupPortal::handleSave() {
    const String ssid = server_.arg("ssid");
    const String pass = server_.arg("pass");
    const String host = server_.arg("host");
    const int    port = server_.arg("port").toInt();

    if (ssid.isEmpty() || host.isEmpty()) {
        server_.send(400, "text/plain", "SSID and hub host are required");
        return;
    }

    cfg_.save(ssid.c_str(), pass.c_str(), host.c_str(), port > 0 ? port : 5000);

    server_.send_P(200, "text/html", kSavedHtml);

#if __has_include(<Arduino.h>)
    Serial.println("[PORTAL] Config saved. Rebooting...");
    delay(1500);
    ESP.restart();
#endif
    done_ = true;
}
#endif