#pragma once

// ── DeviceConfig ──────────────────────────────────────────────────────────────
//
// Loads and saves runtime configuration (WiFi credentials, hub host/port)
// from ESP32 flash using the Preferences library.
//
// compile-time constants (pins, NEC bytes, intervals) stay in prefferences.h
// Runtime credentials that the user fills in live here.
//
// Usage:
//   DeviceConfig cfg;
//   cfg.load();                     // call once in setup()
//   if (!cfg.hasWifi()) {           // first boot — no credentials saved
//       // open captive portal
//   }
//   cfg.wifiSsid()                  // "MyNetwork"
//   cfg.hubHost()                   // "192.168.1.45"
//   cfg.save("MyNet","pass","192.168.1.45", 5000);  // save after portal

#include <cstdint>

#if __has_include(<Preferences.h>)
#include <Preferences.h>
#define DEVICECONFIG_HAS_PREFS 1
#else
#define DEVICECONFIG_HAS_PREFS 0
#endif

class DeviceConfig {
public:
    static constexpr size_t kMaxSsidLen     = 64;
    static constexpr size_t kMaxPasswordLen = 64;
    static constexpr size_t kMaxHostLen     = 64;

    // Load saved config from flash. Call once in setup().
    // Returns true if WiFi credentials were found.
    bool load();

    // Save new config to flash. Call after portal form submit.
    void save(const char* ssid,
              const char* password,
              const char* hubHost,
              int         hubPort);

    // Clear all saved config (factory reset).
    void clear();

    // True if SSID has been saved (i.e. not first boot).
    bool hasWifi() const;

    const char* wifiSsid()     const { return ssid_; }
    const char* wifiPassword() const { return password_; }
    const char* hubHost()      const { return hubHost_; }
    int         hubPort()      const { return hubPort_; }

private:
    char ssid_[kMaxSsidLen]         = {};
    char password_[kMaxPasswordLen] = {};
    char hubHost_[kMaxHostLen]      = {};
    int  hubPort_                   = 5000;
};