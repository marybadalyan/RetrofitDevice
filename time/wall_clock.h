#pragma once

#include <cstdint>

struct WallClockSnapshot {
    uint32_t bootMs = 0;
    uint32_t bootUs = 0;
    bool valid = false;

    uint64_t unixMs = 0;
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;  // 0=Sunday, 6=Saturday
    uint32_t secondsOfDay = 0;
    uint32_t dateKey = 0;  // YYYYMMDD in local time
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual bool isValid() const = 0;
    virtual WallClockSnapshot now(uint32_t nowMs, uint32_t nowUs) = 0;
};

class NtpClock : public IClock {
public:
    // Starts system NTP sync if supported by target/runtime.
    void beginNtp(const char* timezone, const char* ntp1, const char* ntp2 = nullptr, const char* ntp3 = nullptr);

    // Allows hub or other external source to inject current wall time.
    void setUnixTimeMs(uint64_t unixMs, uint32_t nowMs);

    bool isValid() const override;
    WallClockSnapshot now(uint32_t nowMs, uint32_t nowUs) override;

private:
    bool refreshFromSystemTime(uint32_t nowMs);

    bool valid_ = false;
    bool ntpEnabled_ = false;
    uint64_t baseUnixMs_ = 0;
    uint32_t baseNowMs_ = 0;
};

// Backward-compatible alias for older code paths.
using WallClock = NtpClock;
