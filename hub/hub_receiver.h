#pragma once

#include <array>
#include <cstddef>

#include "../commands.h"

class HubReceiver {
public:
    bool pushMockCommand(Command command);
    bool poll(Command& outCommand);

private:
    static constexpr size_t kQueueSize = 16;
    std::array<Command, kQueueSize> queue_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
};
