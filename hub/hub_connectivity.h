#pragma once

#include <cstddef>
#include <cstdint>

#include "../time/wall_clock.h"
#include "hub_receiver.h"

class HubConnectivity {
public:
    void begin(HubReceiver& hubReceiver, WallClock& wallClock);
    void tick(uint32_t nowMs, HubReceiver& hubReceiver, WallClock& wallClock);

    bool wifiConnected() const;
    bool timeConfigured() const;

private:
    bool lookupTimezoneRuleFromIp(char* outRule, size_t outRuleSize);
    const char* mapIanaToPosix(const char* ianaTz) const;
    bool hasWifiCredentials() const;

    bool wifiStarted_ = false;
    bool timeConfigured_ = false;
    uint32_t lastWifiRetryMs_ = 0;
};
