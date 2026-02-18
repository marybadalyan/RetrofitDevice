#pragma once

#include <array>
#include <cstddef>

#include "../commands.h"

class BlynkBridge {
public:
    void begin();

    bool pollControlCommand(Command& outCommand);

    // Hook this from BLYNK_WRITE handlers.
    bool pushControlCommand(Command command);

private:
    static constexpr size_t kQueueSize = 8;
    std::array<Command, kQueueSize> controlQueue_{};
    size_t controlHead_ = 0;
    size_t controlTail_ = 0;
    size_t controlCount_ = 0;

    bool push(std::array<Command, kQueueSize>& queue, size_t& tail, size_t& count, Command command);
    bool pop(std::array<Command, kQueueSize>& queue, size_t& head, size_t& count, Command& outCommand);
};
