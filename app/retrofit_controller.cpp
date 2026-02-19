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
      logger_(logger) {}

void RetrofitController::begin(bool schedulerEnabled) {
    scheduler_.setEnabled(schedulerEnabled);
}

void RetrofitController::tick(uint32_t nowMs, uint32_t nowUs, const WallClockSnapshot& wallNow) {
    (void)nowUs;
    processIncomingFrames(wallNow);
    handlePendingTimeout(wallNow);

    if (pendingStatus_ != PendingStatus::IDLE) {
        return;
    }

    Command next = Command::NONE;
    LogEventType sourceType = LogEventType::COMMAND_DROPPED;
    if (chooseNextCommand(nowMs, wallNow, next, sourceType)) {
        sendCommand(next, wallNow, sourceType);
    }
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
