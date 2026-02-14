#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../commands.h"

struct ScheduleEntry {
    uint32_t atMs;
    Command command;
};

class CommandScheduler {
public:
    void setEnabled(bool enabled);
    bool enabled() const;

    bool addEntry(uint32_t atMs, Command command);
    bool nextDueCommand(uint32_t nowMs, Command& outCommand);

private:
    static constexpr size_t kMaxEntries = 16;
    std::array<ScheduleEntry, kMaxEntries> entries_{};
    size_t count_ = 0;
    bool enabled_ = false;
};
