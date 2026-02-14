#include "../IRReciever.h"
#include "../IRSender.h"
#include "../logger.h"
#include "../prefferences.h"
#include "heater.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
#endif

namespace {
IRReceiver gIrReceiver;
IRSender gIrSender;
Logger gLogger;
Heater gHeater(22.0F);
RelayDriver gRelay;
TempSensor gSensor;
DisplayDriver gDisplay;
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    gIrReceiver.begin();
    gIrSender.begin();
    gRelay.begin();
    gSensor.begin();
    gDisplay.begin();
}

void loop() {
    DecodedFrame frame{};
    if (gIrReceiver.poll(frame) && !frame.isAck) {
        const bool applied = gHeater.applyCommand(frame.command);
        gLogger.log(millis(), LogEventType::STATE_CHANGE, frame.command, applied);
        if (applied) {
            // Heater acknowledges command execution back to retrofit device.
            gIrSender.sendAck(frame.command);
        }
    }

    const float currentTemp = gSensor.readTemperatureC();
    gRelay.setEnabled(gHeater.shouldHeat(currentTemp));
    gDisplay.showTemperature(currentTemp, gHeater.targetTemperatureC(), gHeater.isOn());
}
