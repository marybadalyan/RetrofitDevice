#pragma once

class RoomTempSensor {
public:
    void begin();
    float readTemperatureC();

private:
    float mockTemperatureC_ = 21.5F;
};
