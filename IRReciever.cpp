#include "IRReciever.h"
#include "prefferences.h"
#include "protocol.h"

#if __has_include(<Arduino.h>)

#define RECORD_GAP_MICROS 5000  // ignore NEC repeat bursts
#include <IRremote.hpp>
#include <Arduino.h>

void IRReceiver::begin() {
    IrReceiver.begin(kIrRxPin, DISABLE_LED_FEEDBACK);
    Serial.printf("[IR-RX] Ready on GPIO %d\n", kIrRxPin);
}

bool IRReceiver::poll(DecodedFrame& outFrame) {
    if (!IrReceiver.decode()) return false;

    auto& d = IrReceiver.decodedIRData;

    if ((d.flags & IRDATA_FLAGS_IS_REPEAT) || d.protocol == UNKNOWN) {
        IrReceiver.resume();
        return false;
    }

    const uint8_t cmdByte = static_cast<uint8_t>(d.command & 0xFF);
    IrReceiver.resume();

    Command cmd = Command::NONE;
    if (!protocol::decodeCommand(cmdByte, cmd)) {
        Serial.printf("[IR-RX] unknown cmd=0x%02X\n", cmdByte);
        return false;
    }

    Serial.printf("[IR-RX] cmd=0x%02X -> %s\n", cmdByte, commandToString(cmd));
    outFrame.command = cmd;
    return true;
}

#else
// Desktop stub
void IRReceiver::begin() {}
bool IRReceiver::poll(DecodedFrame&) { return false; }
#endif
