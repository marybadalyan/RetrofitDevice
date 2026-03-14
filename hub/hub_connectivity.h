#pragma once

#include <cstddef>
#include <cstdint>

#include "../time/wall_clock.h"
#include "hub_receiver.h"
#include "DeviceConfig.h"

class HubConnectivity {
public:
    // Pass DeviceConfig so WiFi credentials come from flash, not prefferences.h
    void begin(const DeviceConfig& cfg, HubReceiver& hubReceiver, NtpClock& wallClock);
    void tick(uint32_t nowMs, HubReceiver& hubReceiver, NtpClock& wallClock);

    bool wifiConnected() const;
    bool timeConfigured() const;

private:
    bool lookupTimezoneRuleFromIp(char* outRule, size_t outRuleSize);
    const char* mapIanaToPosix(const char* ianaTz) const;

    // Stored from DeviceConfig at begin()
    char ssid_[DeviceConfig::kMaxSsidLen]         = {};
    char password_[DeviceConfig::kMaxPasswordLen] = {};

    bool wifiStarted_    = false;
    bool timeConfigured_ = false;
    uint32_t lastWifiRetryMs_ = 0;
};