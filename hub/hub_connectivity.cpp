#include "hub_connectivity.h"

#include "../diagnostics/diag.h"
#include "../prefferences.h"

#include <cctype>
#include <cstdio>
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

bool extractJsonInt(const String& payload, const char* key, int32_t& outValue) {
    if (!key) return false;
    const String pattern = String("\"") + key + "\"";
    const int keyPos = payload.indexOf(pattern);
    if (keyPos < 0) return false;
    const int colonPos = payload.indexOf(':', keyPos + pattern.length());
    if (colonPos < 0) return false;

    int start = colonPos + 1;
    while (start < payload.length() && payload[start] == ' ') ++start;
    if (start >= payload.length()) return false;

    int end = start;
    if (payload[end] == '-') ++end;
    while (end < payload.length() && isdigit(static_cast<unsigned char>(payload[end]))) ++end;
    if (end == start || (payload[start] == '-' && end == start + 1)) return false;

    outValue = payload.substring(start, end).toInt();
    return true;
}

bool buildPosixTzFromOffsetSeconds(int32_t offsetSeconds,
                                   char* outRule,
                                   size_t outRuleSize) {
    if (!outRule || outRuleSize == 0) return false;

    const bool localTimeAheadOfUtc = offsetSeconds >= 0;
    uint32_t remaining = static_cast<uint32_t>(localTimeAheadOfUtc ? offsetSeconds : -offsetSeconds);
    const uint32_t hours = remaining / 3600U;
    remaining %= 3600U;
    const uint32_t minutes = remaining / 60U;
    const uint32_t seconds = remaining % 60U;
    const char posixSign = localTimeAheadOfUtc ? '-' : '+';
    int written = 0;

    if (seconds != 0U) {
        written = snprintf(outRule, outRuleSize, "UTC%c%lu:%02lu:%02lu",
                           posixSign,
                           static_cast<unsigned long>(hours),
                           static_cast<unsigned long>(minutes),
                           static_cast<unsigned long>(seconds));
    } else if (minutes != 0U) {
        written = snprintf(outRule, outRuleSize, "UTC%c%lu:%02lu",
                           posixSign,
                           static_cast<unsigned long>(hours),
                           static_cast<unsigned long>(minutes));
    } else {
        written = snprintf(outRule, outRuleSize, "UTC%c%lu",
                           posixSign,
                           static_cast<unsigned long>(hours));
    }

    return written > 0 && static_cast<size_t>(written) < outRuleSize;
}
#endif
} // namespace
void HubConnectivity::begin(HubReceiver& hubReceiver, NtpClock& wallClock) {
    // copyStr(ssid_,     sizeof(ssid_),     kWifiSsid);
    // copyStr(password_, sizeof(password_), kWifiPassword);

#if HUB_HAS_WIFI
    // WiFi may already be connected from setup() — don't reconnect
    if (WiFi.status() == WL_CONNECTED) {
        wifiStarted_ = true;
        wifiLoggedConnected_ = true;
    } else if (ssid_[0] != '\0') {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid_, password_);
        wifiStarted_ = true;
    }
#endif
    (void)hubReceiver;
    (void)wallClock;
}

void HubConnectivity::tick(uint32_t nowMs,
                           HubReceiver& hubReceiver,
                           NtpClock& wallClock) {
    (void)hubReceiver;

#if HUB_HAS_WIFI
    // Don't run tick logic in AP mode
    if (WiFi.getMode() == WIFI_AP) return;

    if (wifiStarted_ && WiFi.status() != WL_CONNECTED) {
        if (nowMs - lastWifiRetryMs_ >= kWifiRetryMs) {
            lastWifiRetryMs_ = nowMs;
            WiFi.disconnect();
            WiFi.begin(ssid_, password_);
            // Only log once every 30s to avoid spam
            static uint32_t lastWarnMs = 0;
            if (nowMs - lastWarnMs >= 30000U) {
                lastWarnMs = nowMs;
                Serial.println("[WIFI] Reconnecting...");
            }
        }
        return;
    }

    // Log when we first confirm connected in tick()
    if (wifiStarted_ && WiFi.status() == WL_CONNECTED && !wifiLoggedConnected_) {
        wifiLoggedConnected_ = true;
        Serial.print("[WIFI] Online — IP: ");
        Serial.println(WiFi.localIP());
    }

    if (!timeConfigured_) {
        if (!wallClock.isValid()) {
            char tzRule[96] = {0};
            copyStr(tzRule, sizeof(tzRule), kNtpTimezone);
            wallClock.beginNtp(tzRule, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);
            Serial.println("[TIME] NTP configured");
        } else {
            Serial.println("[TIME] Wall clock already set — skipping NTP reconfigure");
        }
        timeConfigured_ = true;
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
    if (!outRule || outRuleSize == 0) return false;

    HTTPClient http;
    http.setConnectTimeout(kHubHttpTimeoutMs);
    http.setTimeout(kHubHttpTimeoutMs);

    if (!http.begin(kIpTimezoneUrl)) {
        Serial.println("[TIME] IP timezone lookup begin() failed");
        return false;
    }

    const int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[TIME] IP timezone lookup failed, HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();

    char status[16] = {0};
    if (!extractJsonString(payload, "status", status, sizeof(status)) ||
        strcmp(status, "success") != 0) {
        Serial.println("[TIME] IP timezone lookup returned non-success status");
        return false;
    }

    char timezone[64] = {0};
    const bool hasTimezone = extractJsonString(payload, "timezone", timezone, sizeof(timezone));
    if (hasTimezone) {
        if (const char* mappedRule = mapIanaToPosix(timezone)) {
            copyStr(outRule, outRuleSize, mappedRule);
            Serial.printf("[TIME] IP timezone %s mapped to %s\n", timezone, outRule);
            return true;
        }
    }

    int32_t offsetSeconds = 0;
    if (extractJsonInt(payload, "offset", offsetSeconds) &&
        buildPosixTzFromOffsetSeconds(offsetSeconds, outRule, outRuleSize)) {
        if (hasTimezone) {
            Serial.printf("[TIME] IP timezone %s using fixed offset rule %s\n", timezone, outRule);
        } else {
            Serial.printf("[TIME] IP timezone offset %ld using fixed rule %s\n",
                          static_cast<long>(offsetSeconds), outRule);
        }
        return true;
    }

    Serial.println("[TIME] IP timezone lookup could not resolve a TZ rule");
    return false;
#else
    (void)outRule;
    (void)outRuleSize;
    return false;
#endif
}

const char* HubConnectivity::mapIanaToPosix(const char* ianaTz) const {
    if (!ianaTz) return nullptr;
    if (strcmp(ianaTz, "Asia/Yerevan") == 0)         return "AMT-4";
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
