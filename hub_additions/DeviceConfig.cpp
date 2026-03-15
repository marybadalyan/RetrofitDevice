#include "DeviceConfig.h"

#include <cstring>
#include <cstdio>

#if DEVICECONFIG_HAS_PREFS

static constexpr const char* kNamespace = "device-cfg";
static constexpr const char* kKeySsid   = "ssid";
static constexpr const char* kKeyPass   = "pass";
static constexpr const char* kKeyHost   = "hub_host";
static constexpr const char* kKeyPort   = "hub_port";

bool DeviceConfig::load() {
    Preferences prefs;
    // false = read-write — creates namespace if it doesn't exist yet
    prefs.begin(kNamespace, false);  // ← was true

    prefs.getString(kKeySsid, ssid_,     sizeof(ssid_));
    prefs.getString(kKeyPass, password_, sizeof(password_));
    prefs.getString(kKeyHost, hubHost_,  sizeof(hubHost_));
    hubPort_ = prefs.getInt(kKeyPort, 5000);

    prefs.end();
    return hasWifi();
}

void DeviceConfig::save(const char* ssid,
                        const char* password,
                        const char* hubHost,
                        int         hubPort) {
    // Copy into local buffers
    strncpy(ssid_,     ssid     ? ssid     : "", sizeof(ssid_)     - 1);
    strncpy(password_, password ? password : "", sizeof(password_) - 1);
    strncpy(hubHost_,  hubHost  ? hubHost  : "", sizeof(hubHost_)  - 1);
    hubPort_ = hubPort;

    // Persist to flash
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putString(kKeySsid, ssid_);
    prefs.putString(kKeyPass, password_);
    prefs.putString(kKeyHost, hubHost_);
    prefs.putInt   (kKeyPort, hubPort_);
    prefs.end();
}

void DeviceConfig::clear() {
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
    memset(ssid_,     0, sizeof(ssid_));
    memset(password_, 0, sizeof(password_));
    memset(hubHost_,  0, sizeof(hubHost_));
    hubPort_ = 5000;
}

#else

// ── Stub for non-Arduino builds (unit tests, desktop) ────────────────────────
bool DeviceConfig::load()                                          { return false; }
void DeviceConfig::save(const char*, const char*, const char*, int) {}
void DeviceConfig::clear()                                         {}

#endif

bool DeviceConfig::hasWifi() const {
    return ssid_[0] != '\0';
}