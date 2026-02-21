#include "wall_clock.h"

#include <cstdlib>
#include <cstdint>

#if __has_include(<time.h>)
#include <time.h>
#endif

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

namespace {
constexpr uint32_t kUnixSanityFloor = 1700000000UL;

#if __has_include(<time.h>)
uint32_t makeDateKey(const tm& t) {
    const uint32_t year = static_cast<uint32_t>(t.tm_year + 1900);
    const uint32_t month = static_cast<uint32_t>(t.tm_mon + 1);
    const uint32_t day = static_cast<uint32_t>(t.tm_mday);
    return (year * 10000UL) + (month * 100UL) + day;
}
#endif
}  // namespace

void NtpClock::beginNtp(const char* timezone,
                        const char* ntp1,
                        const char* ntp2,
                        const char* ntp3) {
#if __has_include(<Arduino.h>) && __has_include(<time.h>)
    if (timezone != nullptr) {
        setenv("TZ", timezone, 1);
        tzset();
    }

    if (ntp1 != nullptr) {
        configTime(0, 0, ntp1, ntp2, ntp3);
        ntpEnabled_ = true;
    }
#else
    (void)timezone;
    (void)ntp1;
    (void)ntp2;
    (void)ntp3;
#endif
}

void NtpClock::setUnixTimeMs(uint64_t unixMs, uint32_t nowMs) {
    baseUnixMs_ = unixMs;
    baseNowMs_ = nowMs;
    valid_ = true;
}

bool NtpClock::isValid() const {
    return valid_;
}

bool NtpClock::refreshFromSystemTime(uint32_t nowMs) {
#if __has_include(<time.h>)
    const time_t current = time(nullptr);
    if (current <= static_cast<time_t>(kUnixSanityFloor)) {
        return false;
    }

    setUnixTimeMs(static_cast<uint64_t>(current) * 1000ULL, nowMs);
    return true;
#else
    (void)nowMs;
    return false;
#endif
}

WallClockSnapshot NtpClock::now(uint32_t nowMs, uint32_t nowUs) {
    WallClockSnapshot out{};
    out.bootMs = nowMs;
    out.bootUs = nowUs;

    if (!valid_ && ntpEnabled_) {
        refreshFromSystemTime(nowMs);
    }

    if (!valid_) {
        return out;
    }

    const uint32_t elapsedMs = nowMs - baseNowMs_;
    out.valid = true;
    out.unixMs = baseUnixMs_ + static_cast<uint64_t>(elapsedMs);

#if __has_include(<time.h>)
    const time_t unixSeconds = static_cast<time_t>(out.unixMs / 1000ULL);
    tm localTime{};
    if (localtime_r(&unixSeconds, &localTime) != nullptr) {
        out.year = static_cast<uint16_t>(localTime.tm_year + 1900);
        out.month = static_cast<uint8_t>(localTime.tm_mon + 1);
        out.day = static_cast<uint8_t>(localTime.tm_mday);
        out.hour = static_cast<uint8_t>(localTime.tm_hour);
        out.minute = static_cast<uint8_t>(localTime.tm_min);
        out.second = static_cast<uint8_t>(localTime.tm_sec);
        out.weekday = static_cast<uint8_t>(localTime.tm_wday);
        out.secondsOfDay =
            (static_cast<uint32_t>(out.hour) * 3600UL) + (static_cast<uint32_t>(out.minute) * 60UL) + out.second;
        out.dateKey = makeDateKey(localTime);
    }
#endif

    return out;
}
