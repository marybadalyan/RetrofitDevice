#include <Arduino.h>
#define RECORD_GAP_MICROS 5000  // cut off before NEC repeat bursts (default 8000 was too long)
#include <IRremote.hpp>

// *** CHECK THIS PIN — must match the DATA/OUT pin of your IR receiver ***
#define IR_RECEIVE_PIN 15

void setup() {
    Serial.begin(115200);
    delay(2000);
    IrReceiver.begin(IR_RECEIVE_PIN, DISABLE_LED_FEEDBACK);
}

void loop() {
    if (IrReceiver.decode()) {
        if (IrReceiver.decodedIRData.protocol != UNKNOWN &&
            !(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {

            uint16_t cmd = IrReceiver.decodedIRData.command;

            Serial.print("0x");
            Serial.println(cmd, HEX);
        }
        IrReceiver.resume();
    }
}
