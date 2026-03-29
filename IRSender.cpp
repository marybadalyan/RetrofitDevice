#include "IRSender.h"

#include "IRLearner.h"
#include "prefferences.h"

// IRremote.hpp is intentionally NOT included here — it is a header-only library
// with non-inline definitions, so including it in more than one TU causes ODR
// (multiple-definition) link errors.  All calls into IrSender / IrReceiver are
// routed through IRLearner, which is the single translation unit that owns the
// IRremote include.

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#define IRSENDER_HW 1
#else
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

TxFailureCode IRSender::sendLearnedCode(Command command) {
    if (!learner_) return TxFailureCode::INVALID_COMMAND;

    LearnedCode code;
    if (!learner_->getCode(command, code)) return TxFailureCode::INVALID_COMMAND;

    Serial.printf("[IR] Sending learned: %s proto=%d addr=0x%04X cmd=0x%04X\n",
                  commandToString(command), code.protocol, code.address, code.command);
    learner_->sendCodeDirect(code.protocol, code.address, code.command);
    return TxFailureCode::NONE;
}

TxFailureCode IRSender::sendCommand(Command command) {
    if (!isValidCommand(command))  return TxFailureCode::INVALID_COMMAND;
    if (!hardwareAvailable_)       return TxFailureCode::HW_UNAVAILABLE;
    if (!initialized_)             return TxFailureCode::NOT_INITIALIZED;

    // Only send learned codes — no hardcoded NEC fallback.
    // The user must learn their remote first; sending a wrong protocol
    // (e.g. NEC to a Samsung heater) would be worse than doing nothing.
    if (!learner_ || !learner_->hasLearned(command)) {
        Serial.printf("[IR] No learned code for %s — learn the remote first\n",
                      commandToString(command));
        return TxFailureCode::INVALID_COMMAND;
    }

    return sendLearnedCode(command);
}
