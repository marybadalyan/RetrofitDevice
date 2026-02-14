#include "scheduler.h"

void CommandScheduler::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool CommandScheduler::enabled() const {
    return enabled_;
}

bool CommandScheduler::addEntry(uint32_t atMs, Command command) {
    if (count_ >= entries_.size()) {
        return false;
    }
    entries_[count_++] = ScheduleEntry{atMs, command};
    return true;
}

bool CommandScheduler::nextDueCommand(uint32_t nowMs, Command& outCommand) {
    if (!enabled_) {
        return false;
    }

    size_t dueIndex = entries_.size();
    uint32_t bestTime = 0;
    for (size_t i = 0; i < count_; ++i) {
        if (entries_[i].atMs <= nowMs) {
            if (dueIndex == entries_.size() || entries_[i].atMs < bestTime) {
                dueIndex = i;
                bestTime = entries_[i].atMs;
            }
        }
    }

    if (dueIndex == entries_.size()) {
        return false;
    }

    outCommand = entries_[dueIndex].command;
    entries_[dueIndex] = entries_[count_ - 1];
    --count_;
    return true;
}
