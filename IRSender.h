#pragma once

#include <cstdint>

#include "commands.h"

// Forward-declare so IRSender.h doesn't force everyone to include IRLearner.h
class IRLearner;

enum class TxFailureCode : uint8_t {
    NONE = 0,
    NOT_INITIALIZED = 1, // before begin()
    INVALID_COMMAND = 2,
    INVALID_CONFIG = 3, // means runtime config values needed for TX are invalid
    HW_UNAVAILABLE = 4
};

class IRSender {
public:
    void begin();

    // Optional: attach a learner so sendCommand() can replay learned codes.
    void setLearner(IRLearner* learner) { learner_ = learner; }

    TxFailureCode sendCommand(Command command);

private:
    void mark(uint32_t timeMicros);
    void space(uint32_t timeMicros);
    void sendBit(bool bit);
    void sendByte(uint8_t data);
    TxFailureCode sendFrame(Command command);
    TxFailureCode sendLearnedCode(Command command);

    bool hardwareAvailable_ = true; // assume true until we check
    bool initialized_ = false;
    IRLearner* learner_ = nullptr;
};
