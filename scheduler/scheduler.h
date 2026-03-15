#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../commands.h"
#include "../time/wall_clock.h"

constexpr uint8_t kWeekdaySunday = 1U << 0;
constexpr uint8_t kWeekdayMonday = 1U << 1;
constexpr uint8_t kWeekdayTuesday = 1U << 2;
constexpr uint8_t kWeekdayWednesday = 1U << 3;
constexpr uint8_t kWeekdayThursday = 1U << 4;
constexpr uint8_t kWeekdayFriday = 1U << 5;
constexpr uint8_t kWeekdaySaturday = 1U << 6;
constexpr uint8_t kWeekdayAll = 0x7FU;
constexpr uint8_t kWeekdayWeekdays =
    kWeekdayMonday | kWeekdayTuesday | kWeekdayWednesday | kWeekdayThursday | kWeekdayFriday;
constexpr uint8_t kWeekdayWeekend = kWeekdaySaturday | kWeekdaySunday;

enum class ScheduleMode : uint8_t {
    RELATIVE_ONCE = 0,
    DAILY_WALL_CLOCK = 1,
};

struct ScheduleEntry {
    bool active = false;
    ScheduleMode mode = ScheduleMode::RELATIVE_ONCE;
    Command command = Command::NONE;

    uint32_t atMs = 0;

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekdayMask = kWeekdayAll;
    uint32_t lastFiredDateKey = 0;
};

class CommandScheduler {
public:
    void setEnabled(bool enabled);
    bool enabled() const;

    bool addEntry(uint32_t atMs, Command command);
    bool addDailyEntry(uint8_t hour,
                       uint8_t minute,
                       uint8_t second,
                       Command command,
                       uint8_t weekdayMask = kWeekdayAll);

    bool nextDueCommand(uint32_t nowMs, const WallClockSnapshot& wallNow, Command& outCommand);
    bool nextPlannedCommand(uint32_t nowMs,
                            const WallClockSnapshot& wallNow,
                            Command& outCommand,
                            uint32_t& outDueInSec,
                            bool& outUsesWallClock) const;

private:
    static constexpr size_t kMaxEntries = 16;

    size_t findFreeSlot() const;

    std::array<ScheduleEntry, kMaxEntries> entries_{};
    bool enabled_ = false;
};
