#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"

struct LogEntry {
    uint32_t timestampMs;
    Command command;
};

class Logger {
public:
    void log(Command command, uint32_t timestampMs);
    const std::array<LogEntry, 100>& entries() const;
    size_t size() const;

private:
    std::array<LogEntry, 100> entries_{};
    size_t nextIndex_ = 0;
    size_t size_ = 0;
};
