#include "room_temp_sensor.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
#endif

void RoomTempSensor::begin() {}

float RoomTempSensor::readTemperatureC() {
    // Mock room temperature drift for environments without a real sensor.
    const uint32_t phase = millis() % 7U;
    if (phase == 0U) {
        mockTemperatureC_ += 0.05F;
    } else if (phase == 4U) {
        mockTemperatureC_ -= 0.08F;
    }
    return mockTemperatureC_;
}
