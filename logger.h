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
    HUB_COMMAND_RX = 3, // command rescieved from hub 
    SCHEDULE_COMMAND = 4,
    STATE_CHANGE = 5,
    THERMOSTAT_CONTROL = 6,
    TRANSMIT_FAILED = 7,
    IR_FRAME_RX = 8, // raw IR frame received from hardware 
    ACK_SENT = 9
};

struct LogEntry {
    uint32_t uptimeMs; // milliseconds since device boot
    uint32_t uptimeUs;
    uint64_t unixMs; // Unix epoch timestamp in milliseconds 
    uint32_t dateKey; //  calendar date as YYYYMMDD
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
    bool wallTimeValid;
    LogEventType type;
    Command command;
    bool success;
    uint8_t detailCode; //  extra status/error code
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

    // for RAM
    std::array<LogEntry, kCapacity> entries_{};
    size_t nextIndex_ = 0;
    size_t size_ = 0;
    bool persistenceReady_ = false;
};
