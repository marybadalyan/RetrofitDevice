#pragma once

#include <cstdint>

#include "commands.h"

class Heater {
public:
    explicit Heater(float initialTargetTemperatureC);

    bool applyCommand(Command command);
    bool isOn() const;
    float targetTemperatureC() const;
    bool shouldHeat(float currentTemperatureC) const;

private:
    bool isOn_ = false;
    float targetTemperatureC_ = 22.0F;
};
