#include "IRReciever.h"
#include "prefferences.h"

#if __has_include(<Arduino.h>)

#define RECORD_GAP_MICROS 5000  // ignore NEC repeat bursts
#include <IRremote.hpp>
#include <Arduino.h>

namespace {
constexpr uint8_t kCmdTempUp   = 0x46;
constexpr uint8_t kCmdTempDown = 0x15;
constexpr uint8_t kCmdToggle   = 0x40;

bool decodeCommand(uint8_t byte, Command& out) {
    if (byte == kCmdToggle)   { out = Command::ON_OFF;    return true; }
    if (byte == kCmdTempUp)   { out = Command::TEMP_UP;   return true; }
    if (byte == kCmdTempDown) { out = Command::TEMP_DOWN; return true; }
    return false;
}
} // namespace

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
    if (!decodeCommand(cmdByte, cmd)) {
        Serial.printf("[IR-RX] unknown cmd=0x%02X\n", cmdByte);
        return false;
    }

    const uint32_t now = millis();
    if (cmd == lastCmd_ && (now - lastCmdMs_) < kDebouncMs) {
        return false;
    }
    lastCmd_   = cmd;
    lastCmdMs_ = now;

    Serial.printf("[IR-RX] cmd=0x%02X -> %s\n", cmdByte, commandToString(cmd));
    outFrame.command = cmd;
    return true;
}

#else
// Desktop stub
void IRReceiver::begin() {}
bool IRReceiver::poll(DecodedFrame&) { return false; }
#endif
