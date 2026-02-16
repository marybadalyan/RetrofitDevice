#pragma once

#include <array>
#include <cstddef>

#include "../commands.h"

class BlynkBridge {
public:
    void begin();

    bool pollLearnRequest(Command& outCommand);
    bool pollControlCommand(Command& outCommand);

    // Hook these from BLYNK_WRITE handlers.
    bool pushLearnRequest(Command command);
    bool pushControlCommand(Command command);

private:
    static constexpr size_t kQueueSize = 8;
    std::array<Command, kQueueSize> learnQueue_{};
    std::array<Command, kQueueSize> controlQueue_{};
    size_t learnHead_ = 0;
    size_t learnTail_ = 0;
    size_t learnCount_ = 0;
    size_t controlHead_ = 0;
    size_t controlTail_ = 0;
    size_t controlCount_ = 0;

    bool push(std::array<Command, kQueueSize>& queue, size_t& tail, size_t& count, Command command);
    bool pop(std::array<Command, kQueueSize>& queue, size_t& head, size_t& count, Command& outCommand);
};
