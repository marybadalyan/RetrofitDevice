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

namespace {
bool isValidCommand(Command command) {
    switch (command) {
        case Command::ON:
        case Command::OFF:
        case Command::TEMP_UP:
        case Command::TEMP_DOWN:
            return true;
        default:
            return false;
    }
}
}  // namespace

void IRSender::begin() {
    hardwareAvailable_ = false;
#if __has_include(<Arduino.h>)
    hardwareAvailable_ = true;
#endif

    if (!hardwareAvailable_) {
        initialized_ = false;
        return;
    }

    if (kIrTxPin < 0 || kIrCarrierFreqHz == 0U || kIrPwmResolutionBits == 0U) {
        initialized_ = false;
        return;
    }
#if __has_include(<Arduino.h>)
    ledcSetup(kIrPwmChannel, kIrCarrierFreqHz, kIrPwmResolutionBits);
    ledcAttachPin(kIrTxPin, kIrPwmChannel);
#endif
    initialized_ = true;
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

TxFailureCode IRSender::sendFrame(uint8_t commandByte) {
    (void)commandByte;
    if (!hardwareAvailable_) {
        return TxFailureCode::HW_UNAVAILABLE;
    }

    if (!initialized_) {
        return TxFailureCode::NOT_INITIALIZED;
    }

    if (kIrCarrierFreqHz == 0U || kIrPwmResolutionBits == 0U) {
        return TxFailureCode::INVALID_CONFIG;
    }

    // Frame: header mark/space + header(2 bytes) + command(1) + checksum(1).
    mark(9000);
    space(4500);

    sendByte(static_cast<uint8_t>(protocol::kHeader >> 8));
    sendByte(static_cast<uint8_t>(protocol::kHeader & 0xFFU));
    sendByte(commandByte);
    sendByte(protocol::checksum(commandByte));

    mark(560);
    space(560);
    return TxFailureCode::NONE;
}

TxFailureCode IRSender::sendCommand(Command command) {
    if (!isValidCommand(command)) {
        return TxFailureCode::INVALID_COMMAND;
    }
    return sendFrame(static_cast<uint8_t>(command));
}

TxFailureCode IRSender::sendAck(Command command) {
    if (!isValidCommand(command)) {
        return TxFailureCode::INVALID_COMMAND;
    }
    return sendFrame(static_cast<uint8_t>(static_cast<uint8_t>(command) | 0x80U));
}
