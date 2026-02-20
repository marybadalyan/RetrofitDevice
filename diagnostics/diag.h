#pragma once

#include <cstdint>

#include "../prefferences.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#define DIAG_HAS_SERIAL 1
#else
#define DIAG_HAS_SERIAL 0
#endif

enum class DiagLevel : uint8_t {
    ERROR = 0,
    WARN = 1,
    INFO = 2,
    DEBUG = 3,
};

namespace diag {

inline bool enabled(DiagLevel level) {
    return static_cast<uint8_t>(level) <= kDiagnosticsLogLevel;
}

inline const char* levelLabel(DiagLevel level) {
    switch (level) {
        case DiagLevel::ERROR:
            return "ERROR";
        case DiagLevel::WARN:
            return "WARN";
        case DiagLevel::INFO:
            return "INFO";
        case DiagLevel::DEBUG:
            return "DEBUG";
        default:
            return "UNK";
    }
}

inline void log(DiagLevel level, const char* tag, const char* message) {
#if DIAG_HAS_SERIAL
    if (!enabled(level)) {
        return;
    }

    Serial.print('[');
    Serial.print(levelLabel(level));
    Serial.print("] [");
    Serial.print(tag != nullptr ? tag : "GEN");
    Serial.print("] ");
    Serial.println(message != nullptr ? message : "");
#else
    (void)level;
    (void)tag;
    (void)message;
#endif
}

}  // namespace diag
