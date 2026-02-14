#include "logger.h"

void Logger::log(Command command, uint32_t timestampMs) {
    entries_[nextIndex_] = LogEntry{timestampMs, command};
    nextIndex_ = (nextIndex_ + 1U) % entries_.size();
    if (size_ < entries_.size()) {
        ++size_;
    }
}

const std::array<LogEntry, 100>& Logger::entries() const {
    return entries_;
}

size_t Logger::size() const {
    return size_;
}
