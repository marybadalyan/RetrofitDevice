#include "retrofit_controller.h"

#include "../prefferences.h"

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

    const bool demandHeat = shouldHeat(roomTemperatureC);
    if (demandHeat == heaterCommandedOn_) {
        return;
    }

    sendCommand(demandHeat ? Command::ON : Command::OFF, wallNow, LogEventType::THERMOSTAT_CONTROL);
}

void RetrofitController::sendCommand(Command command, const WallClockSnapshot& wallNow, LogEventType sourceType) {
    logger_.log(wallNow, sourceType, command, true);

    irSender_.sendCommand(command);
    logger_.log(wallNow, LogEventType::COMMAND_SENT, command, true);

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
        irSender_.sendCommand(pendingCommand_);
        logger_.log(wallNow, LogEventType::COMMAND_SENT, pendingCommand_, true);
        pendingDeadlineMs_ = wallNow.bootMs + kAckTimeoutMs;
        return;
    }

    logger_.log(wallNow, LogEventType::COMMAND_DROPPED, pendingCommand_, false);
    pendingStatus_ = PendingStatus::IDLE;
    retryCount_ = 0;
    pendingCommand_ = Command::NONE;
}
