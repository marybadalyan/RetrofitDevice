#pragma once

#include <cstdint>

#include "wall_clock.h"

class MockClock : public IClock {
public:
    MockClock() = default;

    void setSnapshot(const WallClockSnapshot& snapshot) {
        snapshot_ = snapshot;
    }

    void setWallTime(uint32_t dateKey,
                     uint8_t weekday,
                     uint8_t hour,
                     uint8_t minute,
                     uint8_t second,
                     bool valid = true) {
        snapshot_.valid = valid;
        snapshot_.dateKey = dateKey;
        snapshot_.weekday = weekday;
        snapshot_.hour = hour;
        snapshot_.minute = minute;
        snapshot_.second = second;
        snapshot_.secondsOfDay =
            (static_cast<uint32_t>(hour) * 3600UL) + (static_cast<uint32_t>(minute) * 60UL) + static_cast<uint32_t>(second);
    }

    bool isValid() const override {
        return snapshot_.valid;
    }

    WallClockSnapshot now(uint32_t nowMs, uint32_t nowUs) override {
        WallClockSnapshot out = snapshot_;
        out.bootMs = nowMs;
        out.bootUs = nowUs;
        return out;
    }

private:
    WallClockSnapshot snapshot_{};
};
