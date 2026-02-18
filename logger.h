#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"

enum class LogEventType : uint8_t {
    COMMAND_SENT = 0,
    ACK_RECEIVED = 1,
    COMMAND_DROPPED = 2,
    HUB_COMMAND_RX = 3,
    SCHEDULE_COMMAND = 4,
    STATE_CHANGE = 5,
    BLYNK_COMMAND_RX = 6
};

struct LogEntry {
    uint32_t timestampMs;
    LogEventType type;
    Command command;
    bool success;
};

class Logger {
public:
    static constexpr size_t kCapacity = 128;

    void log(uint32_t timestampMs, LogEventType type, Command command, bool success);
    const std::array<LogEntry, kCapacity>& entries() const;
    size_t size() const;

private:
    std::array<LogEntry, kCapacity> entries_{};
    size_t nextIndex_ = 0;
    size_t size_ = 0;
};
