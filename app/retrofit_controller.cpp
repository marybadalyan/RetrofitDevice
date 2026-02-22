#include "retrofit_controller.h"

#include "../diagnostics/diag.h"
#include "../prefferences.h"

namespace {
const char* sourceLabel(LogEventType sourceType) {
    switch (sourceType) {
        case LogEventType::HUB_COMMAND_RX:
            return "HUB";
        case LogEventType::SCHEDULE_COMMAND:
            return "SCHEDULE";
        case LogEventType::THERMOSTAT_CONTROL:
            return "THERMOSTAT";
        default:
            return "OTHER";
    }
}
}  // namespace

RetrofitController::RetrofitController(IRSender& irSender,
                                       IRReceiver& irReceiver,
                                       HubReceiver& hubReceiver,
                                       CommandScheduler& scheduler,
                                       Logger& logger)
    : irSender_(irSender),
      irReceiver_(irReceiver),
      hubReceiver_(hubReceiver),
      scheduler_(scheduler),
      logger_(logger),
      targetTemperatureC_(kDefaultTargetTemperatureC) {}

void RetrofitController::begin(bool schedulerEnabled) {
    scheduler_.setEnabled(schedulerEnabled);
}

RetrofitController::HealthSnapshot RetrofitController::healthSnapshot() const {
    HealthSnapshot snapshot{};
    snapshot.powerEnabled = powerEnabled_;
    snapshot.heaterCommandedOn = heaterCommandedOn_;
    snapshot.targetTemperatureC = targetTemperatureC_;
    snapshot.waitingAck = (pendingStatus_ == PendingStatus::WAITING_ACK);
    snapshot.retryCount = retryCount_;
    snapshot.pendingCommand = pendingCommand_;
    snapshot.lastTxFailure = lastTxFailure_;
    return snapshot;
}

void RetrofitController::tick(uint32_t nowMs,
                              uint32_t nowUs,
                              const WallClockSnapshot& wallNow,
                              float roomTemperatureC) {
    (void)nowUs;
    processIncomingFrames(wallNow);
    handlePendingTimeout(wallNow);

    Command next = Command::NONE;
    LogEventType sourceType = LogEventType::COMMAND_DROPPED;
    if (chooseNextCommand(nowMs, wallNow, next, sourceType)) {
        const bool applied = applyThermostatControlCommand(next);
        logger_.log(wallNow, sourceType, next, applied);
    }

    runThermostatLoop(wallNow, roomTemperatureC);
}

void RetrofitController::processIncomingFrames(const WallClockSnapshot& wallNow) {
    DecodedFrame frame{};
    while (irReceiver_.poll(frame)) {
        if (!frame.isAck) {
            continue;
        }

        logger_.log(wallNow, LogEventType::ACK_RECEIVED, frame.command, true);
        if (pendingStatus_ == PendingStatus::WAITING_ACK && frame.command == pendingCommand_) {
            pendingStatus_ = PendingStatus::IDLE;
            heaterCommandedOn_ = (frame.command == Command::ON);
            retryCount_ = 0;
            pendingCommand_ = Command::NONE;
        }
    }
}

bool RetrofitController::chooseNextCommand(uint32_t nowMs,
                                           const WallClockSnapshot& wallNow,
                                           Command& outCommand,
                                           LogEventType& sourceType) {
    if (scheduler_.enabled()) {
        if (scheduler_.nextDueCommand(nowMs, wallNow, outCommand)) {
            sourceType = LogEventType::SCHEDULE_COMMAND;
            return true;
        }
        return false;
    }

    if (hubReceiver_.poll(outCommand)) {
        sourceType = LogEventType::HUB_COMMAND_RX;
        return true;
    }
    return false;
}

bool RetrofitController::applyThermostatControlCommand(Command command) {
    switch (command) {
        case Command::ON:
            powerEnabled_ = true;
            return true;
        case Command::OFF:
            powerEnabled_ = false;
            return true;
        case Command::TEMP_UP:
            targetTemperatureC_ += 1.0F;
            return true;
        case Command::TEMP_DOWN:
            targetTemperatureC_ -= 1.0F;
            return true;
        default:
            return false;
    }
}

bool RetrofitController::shouldHeat(float roomTemperatureC) const {
    if (!powerEnabled_) {
        return false;
    }

    const float lowerBoundC = targetTemperatureC_ - kThermostatHysteresisC;
    const float upperBoundC = targetTemperatureC_ + kThermostatHysteresisC;

    if (heaterCommandedOn_) {
        return roomTemperatureC < upperBoundC;
    }
    return roomTemperatureC <= lowerBoundC;
}

void RetrofitController::runThermostatLoop(const WallClockSnapshot& wallNow, float roomTemperatureC) {
    if (pendingStatus_ != PendingStatus::IDLE) {
        return;
    }
    if (static_cast<int32_t>(wallNow.bootMs - thermostatSuppressedUntilMs_) < 0) {
        return;
    }

    const bool demandHeat = shouldHeat(roomTemperatureC);
    if (demandHeat == heaterCommandedOn_) {
        return;
    }

    sendCommand(demandHeat ? Command::ON : Command::OFF, wallNow, LogEventType::THERMOSTAT_CONTROL);
}

void RetrofitController::sendCommand(Command command, const WallClockSnapshot& wallNow, LogEventType sourceType) {
    logger_.log(wallNow, sourceType, command, true);

    if (diag::enabled(DiagLevel::INFO)) {
        diag::log(DiagLevel::INFO, "TX", "Preparing IR transmit");
#if __has_include(<Arduino.h>)
        Serial.print("  reason=");
        Serial.print(sourceLabel(sourceType));
        Serial.print(" cmd=");
        Serial.println(commandToString(command));
#endif
    }

    const TxFailureCode txResult = irSender_.sendCommand(command);
    if (txResult != TxFailureCode::NONE) {
        lastTxFailure_ = txResult;
        logger_.log(wallNow,
                    LogEventType::TRANSMIT_FAILED,
                    command,
                    false,
                    static_cast<uint8_t>(txResult));
        if (diag::enabled(DiagLevel::ERROR)) {
            diag::log(DiagLevel::ERROR, "TX", "IR transmit failed");
#if __has_include(<Arduino.h>)
            Serial.print("  reason=");
            Serial.print(sourceLabel(sourceType));
            Serial.print(" cmd=");
            Serial.print(commandToString(command));
            Serial.print(" code=");
            Serial.println(static_cast<uint8_t>(txResult));
#endif
        }
        return;
    }
    lastTxFailure_ = TxFailureCode::NONE;

    logger_.log(wallNow, LogEventType::COMMAND_SENT, command, true, static_cast<uint8_t>(TxFailureCode::NONE));

    pendingStatus_ = PendingStatus::WAITING_ACK;
    pendingCommand_ = command;
    pendingDeadlineMs_ = wallNow.bootMs + kAckTimeoutMs;
}

void RetrofitController::handlePendingTimeout(const WallClockSnapshot& wallNow) {
    if (pendingStatus_ != PendingStatus::WAITING_ACK) {
        return;
    }
    if (static_cast<int32_t>(wallNow.bootMs - pendingDeadlineMs_) < 0) {
        return;
    }

    if (retryCount_ < kMaxRetryCount) {
        ++retryCount_;
        if (diag::enabled(DiagLevel::DEBUG)) {
            diag::log(DiagLevel::DEBUG, "TX", "Retrying IR transmit");
#if __has_include(<Arduino.h>)
            Serial.print("  cmd=");
            Serial.print(commandToString(pendingCommand_));
            Serial.print(" retry=");
            Serial.println(retryCount_);
#endif
        }
        const TxFailureCode txResult = irSender_.sendCommand(pendingCommand_);
        if (txResult != TxFailureCode::NONE) {
            lastTxFailure_ = txResult;
            logger_.log(wallNow,
                        LogEventType::TRANSMIT_FAILED,
                        pendingCommand_,
                        false,
                        static_cast<uint8_t>(txResult));
            if (diag::enabled(DiagLevel::ERROR)) {
                diag::log(DiagLevel::ERROR, "TX", "Retry transmit failed");
#if __has_include(<Arduino.h>)
                Serial.print("  cmd=");
                Serial.print(commandToString(pendingCommand_));
                Serial.print(" code=");
                Serial.println(static_cast<uint8_t>(txResult));
#endif
            }
            pendingDeadlineMs_ = wallNow.bootMs + kAckTimeoutMs;
            return;
        }
        lastTxFailure_ = TxFailureCode::NONE;
        logger_.log(wallNow,
                    LogEventType::COMMAND_SENT,
                    pendingCommand_,
                    true,
                    static_cast<uint8_t>(TxFailureCode::NONE));
        pendingDeadlineMs_ = wallNow.bootMs + kAckTimeoutMs;
        return;
    }

    logger_.log(wallNow, LogEventType::COMMAND_DROPPED, pendingCommand_, false);
    thermostatSuppressedUntilMs_ = wallNow.bootMs + kPostDropCooldownMs;
    pendingStatus_ = PendingStatus::IDLE;
    retryCount_ = 0;
    pendingCommand_ = Command::NONE;
}
