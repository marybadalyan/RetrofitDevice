#include "IRReciever.h"
#include "device.h"
#include "heater.h"
#include "logger.h"
#include "prefferences.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>

static inline uint32_t millis() { return 0; }
#endif

namespace {
IRReceiver irReceiver;
IRSender irSender;
RelayDriver relay;
TempSensor sensor;
DisplayDriver display;
Logger logger;
Heater heater(prefferencedTemp);
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    irReceiver.begin();
    irSender.begin();
    relay.begin();
    sensor.begin();
    display.begin();
}

void loop() {
    Command command = Command::NONE;
    if (irReceiver.poll(command)) {
        // App layer maps decoded command to business logic action.
        if (heater.applyCommand(command)) {
            logger.log(command, millis());
            irSender.sendAck(command); 
            // is this right place to put this guy ? 
            //shouldnt i actually get confirmation from current situation like 
            //temp after - temp before ??? this is as if checking if my if else statment works 
            //or maybe add another layer of confirmation 
        }
    }

    const float currentTemp = sensor.readTemperatureC();
    relay.setEnabled(heater.shouldHeat(currentTemp));
    display.showTemperature(currentTemp, heater.targetTemperatureC(), heater.isOn());
}
