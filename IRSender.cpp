#include "IRSender.h"

#include "prefferences.h"
#include "protocol.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline void delayMicroseconds(uint32_t) {}
static inline void ledcSetup(int, int, int) {}
static inline void ledcAttachPin(int, int) {}
static inline void ledcWrite(int, int) {}
#endif

void IRSender::begin() {
    ledcSetup(kIrPwmChannel, kIrCarrierFreqHz, kIrPwmResolutionBits);
    ledcAttachPin(kIrTxPin, kIrPwmChannel);
}

void IRSender::mark(uint32_t timeMicros) {
    ledcWrite(kIrPwmChannel, 128);
    delayMicroseconds(timeMicros);
}

void IRSender::space(uint32_t timeMicros) {
    ledcWrite(kIrPwmChannel, 0);
    delayMicroseconds(timeMicros);
}

void IRSender::sendBit(bool bit) {
    mark(560);
    space(bit ? 1690 : 560);
}

void IRSender::sendByte(uint8_t data) {
    for (int bit = 7; bit >= 0; --bit) {
        sendBit((data & (1U << bit)) != 0U);
    }
}

void IRSender::sendFrame(uint8_t commandByte) {
    // Frame: header mark/space + header(2 bytes) + command(1) + checksum(1).
    mark(9000);
    space(4500);

    sendByte(static_cast<uint8_t>(protocol::kHeader >> 8));
    sendByte(static_cast<uint8_t>(protocol::kHeader & 0xFFU));
    sendByte(commandByte);
    sendByte(protocol::checksum(commandByte));

    mark(560);
    space(560);
}

void IRSender::sendCommand(Command command) {
    sendFrame(static_cast<uint8_t>(command));
}
