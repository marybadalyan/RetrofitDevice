#include "heater.h"

Heater::Heater(float initialTargetTemperatureC)
    : targetTemperatureC_(initialTargetTemperatureC) {}

bool Heater::applyCommand(Command command) {
    switch (command) {
        case Command::ON:
            isOn_ = true;
            return true;
        case Command::OFF:
            isOn_ = false;
            return true;
        case Command::TEMP_UP:
            targetTemperatureC_ += 1.0F;
            return true;
        case Command::TEMP_DOWN:
            targetTemperatureC_ -= 1.0F;
            return true;
        default:
            return false;
    }
}

bool Heater::isOn() const {
    return isOn_;
}

float Heater::targetTemperatureC() const {
    return targetTemperatureC_;
}

bool Heater::shouldHeat(float currentTemperatureC) const {
    return isOn_ && (currentTemperatureC < targetTemperatureC_); // than who is the one actually increasing this shit 
}
