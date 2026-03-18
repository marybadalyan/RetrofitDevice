#include "room_temp_sensor.h"

#ifdef REAL_TEMP_SENSOR

#include <OneWire.h>
#include <DallasTemperature.h>
#include "prefferences.h"

static OneWire oneWire(kTempSensorPin);
static DallasTemperature sensors(&oneWire);

void RoomTempSensor::begin() {
    sensors.begin();
    Serial.println("[TEMP] DS18B20 initialized");
}

float RoomTempSensor::readTemperatureC() {
    sensors.requestTemperatures();
    const float temp = sensors.getTempCByIndex(0);
    if (temp == DEVICE_DISCONNECTED_C) {
        Serial.println("[TEMP] Sensor disconnected!");
        return -999.0f;
    }
    return temp;
}

#else

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
#endif

void RoomTempSensor::begin() {}

float RoomTempSensor::readTemperatureC() {
    const uint32_t phase = millis() % 7U;
    if (phase == 0U) mockTemperatureC_ += 0.05F;
    else if (phase == 4U) mockTemperatureC_ -= 0.08F;
    return mockTemperatureC_;
}

#endif