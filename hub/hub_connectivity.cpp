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

void copyStr(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0 || !src) return;
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

#if HUB_HAS_HTTP && HUB_HAS_WIFI
bool extractJsonString(const String& payload, const char* key,
                       char* outValue, size_t outValueSize) {
    if (!key || !outValue || outValueSize == 0) return false;
    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;
    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;
    const int startQuote = payload.indexOf('"', colonPos + 1);
    if (startQuote < 0) return false;
    const int endQuote = payload.indexOf('"', startQuote + 1);
    if (endQuote < 0) return false;
    const String value = payload.substring(startQuote + 1, endQuote);
    copyStr(outValue, outValueSize, value.c_str());
    return true;
}
#endif
} // namespace

// ── begin() — store credentials, start WiFi ──────────────────────────────────

void HubConnectivity::begin(const DeviceConfig& cfg,
                            HubReceiver& hubReceiver,
                            NtpClock& wallClock) {
    // Copy credentials out of DeviceConfig into local buffers
    copyStr(ssid_,     sizeof(ssid_),     cfg.wifiSsid());
    copyStr(password_, sizeof(password_), cfg.wifiPassword());

#if HUB_HAS_WIFI
    if (ssid_[0] != '\0') {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid_, password_);
        wifiStarted_ = true;
        diag::log(DiagLevel::INFO, "WIFI", "Connecting to AP");
    } else {
        diag::log(DiagLevel::WARN, "WIFI", "SSID empty; Wi-Fi disabled");
    }
#endif
    (void)hubReceiver;
    (void)wallClock;
}

// ── tick() — reconnect + NTP ──────────────────────────────────────────────────

void HubConnectivity::tick(uint32_t nowMs,
                           HubReceiver& hubReceiver,
                           NtpClock& wallClock) {
    (void)hubReceiver;

#if HUB_HAS_WIFI
    if (!wifiStarted_ && ssid_[0] != '\0') {
        if (nowMs - lastWifiRetryMs_ >= kWifiRetryMs) {
            lastWifiRetryMs_ = nowMs;
            WiFi.begin(ssid_, password_);
            wifiStarted_ = true;
            diag::log(DiagLevel::WARN, "WIFI", "Retrying AP connection");
        }
    }

    if (wifiStarted_ && WiFi.status() != WL_CONNECTED) {
        if (nowMs - lastWifiRetryMs_ >= kWifiRetryMs) {
            lastWifiRetryMs_ = nowMs;
            WiFi.disconnect();
            WiFi.begin(ssid_, password_);
            diag::log(DiagLevel::WARN, "WIFI", "AP disconnected; reconnecting");
        }
        return;
    }

    if (!timeConfigured_) {
        char tzRule[96] = {0};
        copyStr(tzRule, sizeof(tzRule), kNtpTimezone);

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

bool HubConnectivity::lookupTimezoneRuleFromIp(char* outRule, size_t outRuleSize) {
#if HUB_HAS_HTTP && HUB_HAS_WIFI
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setConnectTimeout(2500);
    http.setTimeout(2500);
    if (!http.begin(kIpTimezoneUrl)) return false;

    const int status = http.GET();
    if (status != 200) { http.end(); return false; }

    const String payload = http.getString();
    http.end();

    char ianaZone[64] = {0};
    if (!extractJsonString(payload, "timezone", ianaZone, sizeof(ianaZone))) return false;

    const char* mappedRule = mapIanaToPosix(ianaZone);
    if (mappedRule) {
        strncpy(outRule, mappedRule, outRuleSize - 1);
        return true;
    }
    strncpy(outRule, ianaZone, outRuleSize - 1);
    return true;
#else
    (void)outRule; (void)outRuleSize;
    return false;
#endif
}

const char* HubConnectivity::mapIanaToPosix(const char* ianaTz) const {
    if (!ianaTz) return nullptr;
    if (strcmp(ianaTz, "America/Los_Angeles") == 0) return "PST8PDT,M3.2.0/2,M11.1.0/2";
    if (strcmp(ianaTz, "America/Denver")      == 0) return "MST7MDT,M3.2.0/2,M11.1.0/2";
    if (strcmp(ianaTz, "America/Phoenix")     == 0) return "MST7";
    if (strcmp(ianaTz, "America/Chicago")     == 0) return "CST6CDT,M3.2.0/2,M11.1.0/2";
    if (strcmp(ianaTz, "America/New_York")    == 0) return "EST5EDT,M3.2.0/2,M11.1.0/2";
    if (strcmp(ianaTz, "America/Anchorage")   == 0) return "AKST9AKDT,M3.2.0/2,M11.1.0/2";
    if (strcmp(ianaTz, "Pacific/Honolulu")    == 0) return "HST10";
    if (strcmp(ianaTz, "Etc/UTC") == 0 || strcmp(ianaTz, "UTC") == 0) return "UTC0";
    return nullptr;
}