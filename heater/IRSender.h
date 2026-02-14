#pragma once

#include <cstdint>

#include "commands.h"
#include "protocol.h"

class IRSender {
public:
    void begin();
    void sendAck(Command command);

private:
    void mark(uint32_t timeMicros);
    void space(uint32_t timeMicros);
    void sendBit(bool bit);
    void sendByte(uint8_t data);
    void sendPacket(uint8_t command);
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
