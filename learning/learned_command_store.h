#pragma once

#include <array>
#include <cstdint>

#include "../IRReciever.h"
#include "../commands.h"

class LearnedCommandStore {
public:
    void begin();
    bool save(Command command, const RawIRFrame& frame);
    bool load(Command command, RawIRFrame& outFrame) const;
    bool has(Command command) const;

private:
    struct Slot {
        bool valid = false;
        RawIRFrame frame{};
    };

    static constexpr size_t kSlotCount = 4;
    std::array<Slot, kSlotCount> slots_{};

    static bool commandToIndex(Command command, size_t& outIndex);
    void restoreFromStorage();
    void persistSlot(size_t index);
};
