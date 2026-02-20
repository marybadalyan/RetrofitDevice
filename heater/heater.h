#pragma once

#include <cstdint>

#include "../commands.h"

class Heater {
public:
    bool applyCommand(Command command);
    bool powerEnabled() const;

private:
    bool powerEnabled_ = false;
};

class RelayDriver {
public:
    void begin();
    void setEnabled(bool enabled);
};

class DisplayDriver {
public:
    void begin();
    void showPowerState(bool isPowerEnabled);
};
