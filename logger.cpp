#include "logger.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#define LOGGER_HAS_ARDUINO 1
#else
#include <cstdio>
#define LOGGER_HAS_ARDUINO 0
#endif

#include "prefferences.h"

#if __has_include(<Preferences.h>)
#include <Preferences.h>
#define LOGGER_HAS_PREFERENCES 1
#else
#define LOGGER_HAS_PREFERENCES 0
#endif

namespace {
constexpr uint16_t kPersistenceVersion = 2;

struct LoggerPersistentHeader {
    uint16_t version = kPersistenceVersion;
    uint16_t nextIndex = 0;
    uint16_t size = 0;
};

#if LOGGER_HAS_PREFERENCES
Preferences& prefs() {
    static Preferences instance;
    return instance;
}
#endif

const char* eventToString(LogEventType type) {
    switch (type) {
        case LogEventType::COMMAND_SENT:
            return "COMMAND_SENT";
        case LogEventType::ACK_RECEIVED:
            return "ACK_RECEIVED";
        case LogEventType::COMMAND_DROPPED:
            return "COMMAND_DROPPED";
        case LogEventType::HUB_COMMAND_RX:
            return "HUB_COMMAND_RX";
        case LogEventType::SCHEDULE_COMMAND:
            return "SCHEDULE_COMMAND";
        case LogEventType::STATE_CHANGE:
            return "STATE_CHANGE";
        case LogEventType::THERMOSTAT_CONTROL:
            return "THERMOSTAT_CONTROL";
        case LogEventType::TRANSMIT_FAILED:
            return "TRANSMIT_FAILED";
        case LogEventType::IR_FRAME_RX:
            return "IR_FRAME_RX";
        case LogEventType::ACK_SENT:
            return "ACK_SENT";
        default:
            return "UNKNOWN";
    }
}

void printLogEntry(const LogEntry& entry) {
    if (kDiagnosticsLogLevel < 2U) {
        return;
    }

#if LOGGER_HAS_ARDUINO
    Serial.print("[LOG] ");
    if (entry.wallTimeValid) {
        Serial.print(entry.dateKey);
        Serial.print(' ');
        if (entry.hour < 10U) {
            Serial.print('0');
        }
        Serial.print(entry.hour);
        Serial.print(':');
        if (entry.minute < 10U) {
            Serial.print('0');
        }
        Serial.print(entry.minute);
        Serial.print(':');
        if (entry.second < 10U) {
            Serial.print('0');
        }
        Serial.print(entry.second);
    } else {
        Serial.print("bootMs=");
        Serial.print(entry.uptimeMs);
    }

    Serial.print(" evt=");
    Serial.print(eventToString(entry.type));
    Serial.print(" cmd=");
    Serial.print(commandToString(entry.command));
    Serial.print(" success=");
    Serial.print(entry.success ? "1" : "0");
    Serial.print(" code=");
    Serial.println(entry.detailCode);
#else
    if (entry.wallTimeValid) {
        std::printf("[LOG] %u %02u:%02u:%02u evt=%s cmd=%s success=%u code=%u\n",
                    static_cast<unsigned>(entry.dateKey),
                    static_cast<unsigned>(entry.hour),
                    static_cast<unsigned>(entry.minute),
                    static_cast<unsigned>(entry.second),
                    eventToString(entry.type),
                    commandToString(entry.command),
                    static_cast<unsigned>(entry.success ? 1U : 0U),
                    static_cast<unsigned>(entry.detailCode));
    } else {
        std::printf("[LOG] bootMs=%u evt=%s cmd=%s success=%u code=%u\n",
                    static_cast<unsigned>(entry.uptimeMs),
                    eventToString(entry.type),
                    commandToString(entry.command),
                    static_cast<unsigned>(entry.success ? 1U : 0U),
                    static_cast<unsigned>(entry.detailCode));
    }
    std::fflush(stdout);
#endif
}
}  // namespace

void Logger::log(const WallClockSnapshot& timestamp,
                 LogEventType type,
                 Command command,
                 bool success,
                 uint8_t detailCode) {
    entries_[nextIndex_] = LogEntry{
        timestamp.bootMs,
        timestamp.bootUs,
        timestamp.unixMs,
        timestamp.dateKey,
        timestamp.hour,
        timestamp.minute,
        timestamp.second,
        timestamp.weekday,
        timestamp.valid,
        type,
        command,
        success,
        detailCode,
    };

    nextIndex_ = (nextIndex_ + 1U) % entries_.size();
    if (size_ < entries_.size()) {
        ++size_;
    }

    printLogEntry(entries_[(nextIndex_ + entries_.size() - 1U) % entries_.size()]);

    persistState();
}

bool Logger::beginPersistence(const char* storageNamespace) {
#if LOGGER_HAS_PREFERENCES
    if (storageNamespace == nullptr) {
        return false;
    }

    if (!prefs().begin(storageNamespace, false)) {
        return false;
    }

    LoggerPersistentHeader header{};
    const size_t headerRead = prefs().getBytes("header", &header, sizeof(header));
    if (headerRead == sizeof(header) && header.version == kPersistenceVersion) {
        const uint16_t maxCapacity = static_cast<uint16_t>(kCapacity);
        nextIndex_ = (header.nextIndex < maxCapacity) ? header.nextIndex : 0;
        size_ = (header.size <= maxCapacity) ? header.size : 0;

        const size_t expected = sizeof(LogEntry) * entries_.size();
        const size_t entriesRead = prefs().getBytes("entries", entries_.data(), expected);
        if (entriesRead != expected) {
            nextIndex_ = 0;
            size_ = 0;
            entries_.fill(LogEntry{});
        }
    }

    persistenceReady_ = true;
    return true;
#else
    (void)storageNamespace;
    return false;
#endif
}

void Logger::persistState() {
#if LOGGER_HAS_PREFERENCES
    if (!persistenceReady_) {
        return;
    }

    LoggerPersistentHeader header{};
    header.version = kPersistenceVersion;
    header.nextIndex = static_cast<uint16_t>(nextIndex_);
    header.size = static_cast<uint16_t>(size_);

    prefs().putBytes("header", &header, sizeof(header));
    prefs().putBytes("entries", entries_.data(), sizeof(LogEntry) * entries_.size());
#endif
}

const std::array<LogEntry, Logger::kCapacity>& Logger::entries() const {
    return entries_;
}

size_t Logger::size() const {
    return size_;
}
