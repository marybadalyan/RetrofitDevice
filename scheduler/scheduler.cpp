#include "scheduler.h"

namespace {
uint8_t weekdayBit(uint8_t weekday) {
    if (weekday > 6U) {
        return 0;
    }
    return static_cast<uint8_t>(1U << weekday);
}

uint32_t secondsFromHms(uint8_t hour, uint8_t minute, uint8_t second) {
    return (static_cast<uint32_t>(hour) * 3600UL) + (static_cast<uint32_t>(minute) * 60UL) + second;
}
}  // namespace

void CommandScheduler::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool CommandScheduler::enabled() const {
    return enabled_;
}

size_t CommandScheduler::findFreeSlot() const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].active) {
            return i;
        }
    }
    return entries_.size();
}

bool CommandScheduler::addEntry(uint32_t atMs, Command command) {
    const size_t slot = findFreeSlot();
    if (slot == entries_.size()) {
        return false;
    }

    entries_[slot] = ScheduleEntry{};
    entries_[slot].active = true;
    entries_[slot].mode = ScheduleMode::RELATIVE_ONCE;
    entries_[slot].atMs = atMs;
    entries_[slot].command = command;
    return true;
}

bool CommandScheduler::addDailyEntry(uint8_t hour,
                                     uint8_t minute,
                                     uint8_t second,
                                     Command command,
                                     uint8_t weekdayMask) {
    if (hour > 23U || minute > 59U || second > 59U || weekdayMask == 0U) {
        return false;
    }

    const size_t slot = findFreeSlot();
    if (slot == entries_.size()) {
        return false;
    }

    entries_[slot] = ScheduleEntry{};
    entries_[slot].active = true;
    entries_[slot].mode = ScheduleMode::DAILY_WALL_CLOCK;
    entries_[slot].command = command;
    entries_[slot].hour = hour;
    entries_[slot].minute = minute;
    entries_[slot].second = second;
    entries_[slot].weekdayMask = weekdayMask;
    entries_[slot].lastFiredDateKey = 0;
    return true;
}

bool CommandScheduler::nextDueCommand(uint32_t nowMs, const WallClockSnapshot& wallNow, Command& outCommand) {
    if (!enabled_) {
        return false;
    }

    size_t dueRelative = entries_.size();
    uint32_t bestRelativeMs = 0;

    size_t dueDaily = entries_.size();
    uint32_t bestDailySeconds = 0;

    for (size_t i = 0; i < entries_.size(); ++i) {
        ScheduleEntry& entry = entries_[i];
        if (!entry.active) {
            continue;
        }

        if (entry.mode == ScheduleMode::RELATIVE_ONCE) {
            if (entry.atMs <= nowMs) {
                if (dueRelative == entries_.size() || entry.atMs < bestRelativeMs) {
                    dueRelative = i;
                    bestRelativeMs = entry.atMs;
                }
            }
            continue;
        }

        if (!wallNow.valid || wallNow.dateKey == 0U) {
            continue;
        }

        const uint8_t dayBit = weekdayBit(wallNow.weekday);
        if ((entry.weekdayMask & dayBit) == 0U) {
            continue;
        }

        const uint32_t targetSeconds = secondsFromHms(entry.hour, entry.minute, entry.second);
        if (wallNow.secondsOfDay < targetSeconds) {
            continue;
        }

        if (entry.lastFiredDateKey == wallNow.dateKey) {
            continue;
        }

        if (dueDaily == entries_.size() || targetSeconds < bestDailySeconds) {
            dueDaily = i;
            bestDailySeconds = targetSeconds;
        }
    }

    if (dueRelative != entries_.size()) {
        outCommand = entries_[dueRelative].command;
        entries_[dueRelative].active = false;
        return true;
    }

    if (dueDaily != entries_.size()) {
        outCommand = entries_[dueDaily].command;
        entries_[dueDaily].lastFiredDateKey = wallNow.dateKey;
        return true;
    }

    return false;
}
