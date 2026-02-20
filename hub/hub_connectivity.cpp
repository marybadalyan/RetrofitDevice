#include "hub_connectivity.h"

#include "../diagnostics/diag.h"
#include "../prefferences.h"

#include <cstring>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if __has_include(<WiFi.h>)
#include <WiFi.h>
#define HUB_HAS_WIFI 1
#else
#define HUB_HAS_WIFI 0
#endif

#if __has_include(<HTTPClient.h>)
#include <HTTPClient.h>
#define HUB_HAS_HTTP 1
#else
#define HUB_HAS_HTTP 0
#endif

namespace {
constexpr uint32_t kWifiRetryMs = 8000;

void copyRule(char* outRule, size_t outRuleSize, const char* source) {
    if (outRule == nullptr || outRuleSize == 0U || source == nullptr) {
        return;
    }
    strncpy(outRule, source, outRuleSize - 1U);
    outRule[outRuleSize - 1U] = '\0';
}

#if HUB_HAS_HTTP && HUB_HAS_WIFI
bool extractJsonString(const String& payload, const char* key, char* outValue, size_t outValueSize) {
    if (key == nullptr || outValue == nullptr || outValueSize == 0U) {
        return false;
    }

    String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) {
        return false;
    }

    int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) {
        return false;
    }

    int startQuote = payload.indexOf('"', colonPos + 1);
    if (startQuote < 0) {
        return false;
    }

    int endQuote = payload.indexOf('"', startQuote + 1);
    if (endQuote < 0) {
        return false;
    }

    const String value = payload.substring(startQuote + 1, endQuote);
    copyRule(outValue, outValueSize, value.c_str());
    return true;
}
#endif

}  // namespace

void HubConnectivity::begin(HubReceiver& hubReceiver, WallClock& wallClock) {
#if HUB_HAS_WIFI
    if (hasWifiCredentials()) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(kWifiSsid, kWifiPassword);
        wifiStarted_ = true;
        diag::log(DiagLevel::INFO, "WIFI", "Connecting to AP");
    } else {
        diag::log(DiagLevel::WARN, "WIFI", "SSID is empty; Wi-Fi disabled");
    }
#endif

    (void)hubReceiver;
    (void)wallClock;
}

void HubConnectivity::tick(uint32_t nowMs, HubReceiver& hubReceiver, WallClock& wallClock) {
    (void)hubReceiver;

#if HUB_HAS_WIFI
    if (!wifiStarted_ && hasWifiCredentials()) {
        if (nowMs - lastWifiRetryMs_ >= kWifiRetryMs) {
            lastWifiRetryMs_ = nowMs;
            WiFi.begin(kWifiSsid, kWifiPassword);
            wifiStarted_ = true;
            diag::log(DiagLevel::WARN, "WIFI", "Retrying AP connection");
        }
    }

    if (wifiStarted_ && WiFi.status() != WL_CONNECTED) {
        if (nowMs - lastWifiRetryMs_ >= kWifiRetryMs) {
            lastWifiRetryMs_ = nowMs;
            WiFi.disconnect();
            WiFi.begin(kWifiSsid, kWifiPassword);
            diag::log(DiagLevel::WARN, "WIFI", "AP disconnected; reconnecting");
        }
        return;
    }

    if (!timeConfigured_) {
        char tzRule[96] = {0};
        copyRule(tzRule, sizeof(tzRule), kNtpTimezone);

        if (kEnableIpTimezoneLookup) {
            if (!lookupTimezoneRuleFromIp(tzRule, sizeof(tzRule))) {
                diag::log(DiagLevel::WARN, "TIME", "IP timezone lookup failed; using fallback TZ");
            }
        }

        wallClock.beginNtp(tzRule, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);
        timeConfigured_ = true;
        if (diag::enabled(DiagLevel::INFO)) {
            diag::log(DiagLevel::INFO, "TIME", "NTP configured");
#if __has_include(<Arduino.h>)
            Serial.print("  tz=");
            Serial.println(tzRule);
#endif
        }
    }
#endif

}

bool HubConnectivity::wifiConnected() const {
#if HUB_HAS_WIFI
    return wifiStarted_ && (WiFi.status() == WL_CONNECTED);
#else
    return false;
#endif
}

bool HubConnectivity::timeConfigured() const {
    return timeConfigured_;
}

bool HubConnectivity::hasWifiCredentials() const {
    return (kWifiSsid != nullptr && kWifiSsid[0] != '\0');
}

bool HubConnectivity::lookupTimezoneRuleFromIp(char* outRule, size_t outRuleSize) {
#if HUB_HAS_HTTP && HUB_HAS_WIFI
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(2500);

    if (!http.begin(kIpTimezoneUrl)) {
        return false;
    }

    const int status = http.GET();
    if (status != 200) {
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();

    char ianaZone[64] = {0};
    if (!extractJsonString(payload, "timezone", ianaZone, sizeof(ianaZone))) {
        return false;
    }

    const char* mappedRule = mapIanaToPosix(ianaZone);
    if (mappedRule != nullptr) {
        copyRule(outRule, outRuleSize, mappedRule);
        return true;
    }

    // Fallback: pass IANA timezone directly. On targets with zoneinfo support,
    // this enables full location-aware timezone + DST behavior.
    copyRule(outRule, outRuleSize, ianaZone);
    return true;
#else
    (void)outRule;
    (void)outRuleSize;
    return false;
#endif
}

const char* HubConnectivity::mapIanaToPosix(const char* ianaTz) const {
    if (ianaTz == nullptr) {
        return nullptr;
    }

    if (strcmp(ianaTz, "America/Los_Angeles") == 0) {
        return "PST8PDT,M3.2.0/2,M11.1.0/2";
    }
    if (strcmp(ianaTz, "America/Denver") == 0) {
        return "MST7MDT,M3.2.0/2,M11.1.0/2";
    }
    if (strcmp(ianaTz, "America/Phoenix") == 0) {
        return "MST7";
    }
    if (strcmp(ianaTz, "America/Chicago") == 0) {
        return "CST6CDT,M3.2.0/2,M11.1.0/2";
    }
    if (strcmp(ianaTz, "America/New_York") == 0) {
        return "EST5EDT,M3.2.0/2,M11.1.0/2";
    }
    if (strcmp(ianaTz, "America/Anchorage") == 0) {
        return "AKST9AKDT,M3.2.0/2,M11.1.0/2";
    }
    if (strcmp(ianaTz, "Pacific/Honolulu") == 0) {
        return "HST10";
    }
    if (strcmp(ianaTz, "Etc/UTC") == 0 || strcmp(ianaTz, "UTC") == 0) {
        return "UTC0";
    }

    return nullptr;
}
