#include "learned_command_store.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#endif

#if __has_include(<Preferences.h>)
#include <Preferences.h>
#define HAS_PREFERENCES 1
#else
#define HAS_PREFERENCES 0
#endif

namespace {
#if HAS_PREFERENCES
constexpr const char* kNamespace = "learn_ir";

const char* countKeyForIndex(size_t index) {
    switch (index) {
        case 0:
            return "c0";
        case 1:
            return "c1";
        case 2:
            return "c2";
        case 3:
            return "c3";
        default:
            return "cx";
    }
}

const char* dataKeyForIndex(size_t index) {
    switch (index) {
        case 0:
            return "d0";
        case 1:
            return "d1";
        case 2:
            return "d2";
        case 3:
            return "d3";
        default:
            return "dx";
    }
}
#endif
}  // namespace

void LearnedCommandStore::begin() {
    restoreFromStorage();
}

bool LearnedCommandStore::commandToIndex(Command command, size_t& outIndex) {
    switch (command) {
        case Command::ON:
            outIndex = 0;
            return true;
        case Command::OFF:
            outIndex = 1;
            return true;
        case Command::TEMP_UP:
            outIndex = 2;
            return true;
        case Command::TEMP_DOWN:
            outIndex = 3;
            return true;
        default:
            return false;
    }
}

bool LearnedCommandStore::save(Command command, const RawIRFrame& frame) {
    size_t index = 0;
    if (!commandToIndex(command, index)) {
        return false;
    }
    if (frame.count == 0 || frame.count > frame.pulsesUs.size()) {
        return false;
    }

    slots_[index].valid = true;
    slots_[index].frame = frame;
    persistSlot(index);
    return true;
}

bool LearnedCommandStore::load(Command command, RawIRFrame& outFrame) const {
    size_t index = 0;
    if (!commandToIndex(command, index)) {
        return false;
    }
    if (!slots_[index].valid) {
        return false;
    }

    outFrame = slots_[index].frame;
    return true;
}

bool LearnedCommandStore::has(Command command) const {
    size_t index = 0;
    if (!commandToIndex(command, index)) {
        return false;
    }
    return slots_[index].valid;
}

void LearnedCommandStore::restoreFromStorage() {
#if HAS_PREFERENCES
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) {
        return;
    }

    for (size_t i = 0; i < slots_.size(); ++i) {
        const uint16_t count = prefs.getUShort(countKeyForIndex(i), 0);
        if (count == 0 || count > slots_[i].frame.pulsesUs.size()) {
            slots_[i].valid = false;
            continue;
        }

        slots_[i].frame.count = count;
        const size_t byteCount = static_cast<size_t>(count) * sizeof(uint16_t);
        const size_t read = prefs.getBytes(dataKeyForIndex(i), slots_[i].frame.pulsesUs.data(), byteCount);
        slots_[i].valid = (read == byteCount);
    }

    prefs.end();
#endif
}

void LearnedCommandStore::persistSlot(size_t index) {
#if !HAS_PREFERENCES
    (void)index;
#endif
#if HAS_PREFERENCES
    if (index >= slots_.size() || !slots_[index].valid) {
        return;
    }

    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return;
    }

    const uint16_t count = static_cast<uint16_t>(slots_[index].frame.count);
    prefs.putUShort(countKeyForIndex(index), count);
    const size_t byteCount = static_cast<size_t>(count) * sizeof(uint16_t);
    prefs.putBytes(dataKeyForIndex(index), slots_[index].frame.pulsesUs.data(), byteCount);

    prefs.end();
#endif
}
