#include "IRSender.h"

#include "IRLearner.h"
#include "prefferences.h"
#include "protocol.h"

// IRremote.hpp is intentionally NOT included here — it is a header-only library
// with non-inline definitions, so including it in more than one TU causes ODR
// (multiple-definition) link errors.  All calls into IrSender / IrReceiver are
// routed through IRLearner, which is the single translation unit that owns the
// IRremote include.

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#define IRSENDER_HW 1
#else
#include <cstdint>
static inline void delayMicroseconds(uint32_t) {}
static inline void ledcSetup(int, int, int) {}
static inline void ledcAttachPin(int, int) {}
static inline void ledcWrite(int, int) {}
#define IRSENDER_HW 0
#endif

namespace {
bool isValidCommand(Command command) {
    switch (command) {
        case Command::ON_OFF:
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
#if IRSENDER_HW
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

    // IrSender.begin() is called via learner_->beginSend().
    // Ensure setLearner() is called before begin() on hardware builds.
    if (learner_) {
        learner_->beginSend();
    }

    initialized_ = true;
}

// ── Manual NEC bit-bang (desktop / test build only) ───────────────────────────
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
    for (int bit = 0; bit < 8; ++bit) {
        sendBit((data & (1U << bit)) != 0U);
    }
}

TxFailureCode IRSender::sendFrame(Command command) {
    if (!hardwareAvailable_) return TxFailureCode::HW_UNAVAILABLE;
    if (!initialized_)       return TxFailureCode::NOT_INITIALIZED;
    if (kIrCarrierFreqHz == 0U || kIrPwmResolutionBits == 0U)
        return TxFailureCode::INVALID_CONFIG;

    const protocol::Packet packet = protocol::makePacket(command);
    Command parsedCommand = Command::NONE;
    if (!protocol::parsePacket(packet, parsedCommand) || parsedCommand != command)
        return TxFailureCode::INVALID_COMMAND;

#if IRSENDER_HW
    // Route through IRLearner so this TU does not need IRremote.hpp.
    if (learner_) {
        learner_->sendNECDirect(packet.address, packet.command);
        return TxFailureCode::NONE;
    }
    return TxFailureCode::NOT_INITIALIZED;
#else
    // Desktop test build — manual bit-bang (no IRremote available).
    mark(9000);
    space(4500);
    sendByte(packet.address);
    sendByte(packet.addressInverse);
    sendByte(packet.command);
    sendByte(packet.commandInverse);
    mark(560);
    space(40000);
    return TxFailureCode::NONE;
#endif
}

TxFailureCode IRSender::sendLearnedCode(Command command) {
    if (!learner_) return TxFailureCode::INVALID_COMMAND;

    LearnedCode code;
    if (!learner_->getCode(command, code)) return TxFailureCode::INVALID_COMMAND;

    learner_->sendCodeDirect(code.protocol, code.address, code.command);
    return TxFailureCode::NONE;
}

TxFailureCode IRSender::sendCommand(Command command) {
    if (!isValidCommand(command))  return TxFailureCode::INVALID_COMMAND;
    if (!hardwareAvailable_)       return TxFailureCode::HW_UNAVAILABLE;
    if (!initialized_)             return TxFailureCode::NOT_INITIALIZED;

    // Try learned code first — any brand/protocol.
    if (learner_ && learner_->hasLearned(command)) {
        const TxFailureCode rc = sendLearnedCode(command);
        if (rc == TxFailureCode::NONE) return rc;
        // fall through to hardcoded NEC on failure
    }

    return sendFrame(command);
}
