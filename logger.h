#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "commands.h"
#include "time/wall_clock.h"

enum class LogEventType : uint8_t {
    COMMAND_SENT = 0,
    ACK_RECEIVED = 1,
    COMMAND_DROPPED = 2,
    HUB_COMMAND_RX = 3,
    SCHEDULE_COMMAND = 4,
    STATE_CHANGE = 5,
    THERMOSTAT_CONTROL = 6,
    TRANSMIT_FAILED = 7
};

struct LogEntry {
    uint32_t uptimeMs;
    uint32_t uptimeUs;
    uint64_t unixMs;
    uint32_t dateKey;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    bool wallTimeValid;
    LogEventType type;
    Command command;
    bool success;
    uint8_t detailCode;
};

class Logger {
public:
    static constexpr size_t kCapacity = 128;

    void log(const WallClockSnapshot& timestamp,
             LogEventType type,
             Command command,
             bool success,
             uint8_t detailCode = 0);
    bool beginPersistence(const char* storageNamespace);

    const std::array<LogEntry, kCapacity>& entries() const;
    size_t size() const;

private:
    void persistState();

    std::array<LogEntry, kCapacity> entries_{};
    size_t nextIndex_ = 0;
    size_t size_ = 0;
    bool persistenceReady_ = false;
};
