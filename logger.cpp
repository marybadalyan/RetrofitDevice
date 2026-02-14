#include "logger.h"

void Logger::log(uint32_t timestampMs, LogEventType type, Command command, bool success) {
    entries_[nextIndex_] = LogEntry{timestampMs, type, command, success};
    nextIndex_ = (nextIndex_ + 1U) % entries_.size();
    if (size_ < entries_.size()) {
        ++size_;
    }
}

const std::array<LogEntry, Logger::kCapacity>& Logger::entries() const {
    return entries_;
}

size_t Logger::size() const {
    return size_;
}
