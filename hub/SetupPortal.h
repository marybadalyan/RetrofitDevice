#pragma once

// ── SetupPortal ───────────────────────────────────────────────────────────────
//
// Opens a WiFi hotspot "ThermoSetup" and serves a config page at 192.168.4.1.
// The user connects their phone/laptop to ThermoSetup, opens a browser,
// fills in WiFi SSID, password, and hub IP, then submits.
// On submit: saves to DeviceConfig flash, then reboots.
//
// Usage in main.cpp:
//   if (!cfg.hasWifi()) {
//       SetupPortal portal(cfg);
//       portal.begin();
//       while (!portal.done()) { portal.tick(); }
//       // never reaches here — portal reboots on save
//   }

#include "DeviceConfig.h"

#if __has_include(<WebServer.h>)
#include <WebServer.h>
#define PORTAL_HAS_WEBSERVER 1
#else
#define PORTAL_HAS_WEBSERVER 0
#endif

class SetupPortal {
public:
    explicit SetupPortal(DeviceConfig& cfg);

    // Start AP + web server. Call once.
    void begin();

    // Call repeatedly in a loop until done() returns true.
    void tick();

    // True after config has been saved and ESP32 is about to reboot.
    bool done() const { return done_; }

private:
#if PORTAL_HAS_WEBSERVER
    void handleRoot();
    void handleSave();

    WebServer server_{80};
#endif
    DeviceConfig& cfg_;
    bool done_ = false;
};