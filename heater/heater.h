#pragma once

#include <cstdint>

#include "../commands.h"
#include "../logger.h"
#include "../protocol.h"

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


class RelayDriver {
public:
    void begin();
    void setEnabled(bool enabled);
};

class TempSensor {
public:
    void begin();
    float readTemperatureC();

private:
    float mockTemperature_ = 21.5F;
};

class DisplayDriver {
public:
    void begin();
    void showTemperature(float currentTemperatureC, float targetTemperatureC, bool isHeaterOn);
};
