#include "logger.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

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
