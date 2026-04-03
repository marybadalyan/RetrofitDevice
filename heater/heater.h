#pragma once

#include <cstdint>

#include "../commands.h"

class Heater {
public:
    bool applyCommand(Command command);
    bool powerEnabled() const;
    void setPowerEnabled(bool enabled);
private:
    bool powerEnabled_ = true;
};

class DisplayDriver {
public:
    void begin();
    void showPowerState(bool isPowerEnabled);
};

class CommandStatusLed {
public:
    void begin();
    void showCommand(Command command, bool isPowerEnabled);

private:
    void setColor(bool redOn, bool greenOn, bool blueOn);
};

