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

void RetrofitController::tick(uint32_t nowMs) {
    processIncomingFrames(nowMs);
    handlePendingTimeout(nowMs);

    if (pendingStatus_ != PendingStatus::IDLE) {
        return;
    }

    Command next = Command::NONE;
    LogEventType sourceType = LogEventType::COMMAND_DROPPED;
    if (chooseNextCommand(nowMs, next, sourceType)) {
        sendCommand(next, nowMs, sourceType);
    }
}

bool RetrofitController::sendImmediate(Command command, uint32_t nowMs, LogEventType sourceType) {
    processIncomingFrames(nowMs);
    if (pendingStatus_ != PendingStatus::IDLE) {
        logger_.log(nowMs, LogEventType::COMMAND_DROPPED, command, false);
        return false;
    }

    sendCommand(command, nowMs, sourceType);
    return true;
}

void RetrofitController::processIncomingFrames(uint32_t nowMs) {
    DecodedFrame frame{};
    while (irReceiver_.poll(frame)) {
        if (!frame.isAck) {
            continue;
        }

        logger_.log(nowMs, LogEventType::ACK_RECEIVED, frame.command, true);
        if (pendingStatus_ == PendingStatus::WAITING_ACK && frame.command == pendingCommand_) {
            pendingStatus_ = PendingStatus::IDLE;
            retryCount_ = 0;
            pendingCommand_ = Command::NONE;
        }
    }
}

bool RetrofitController::chooseNextCommand(uint32_t nowMs, Command& outCommand, LogEventType& sourceType) {
    if (scheduler_.enabled()) {
        if (scheduler_.nextDueCommand(nowMs, outCommand)) {
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

void RetrofitController::sendCommand(Command command, uint32_t nowMs, LogEventType sourceType) {
    logger_.log(nowMs, sourceType, command, true);

    irSender_.sendCommand(command);
    logger_.log(nowMs, LogEventType::COMMAND_SENT, command, true);

    pendingStatus_ = PendingStatus::WAITING_ACK;
    pendingCommand_ = command;
    pendingDeadlineMs_ = nowMs + kAckTimeoutMs;
}

void RetrofitController::handlePendingTimeout(uint32_t nowMs) {
    if (pendingStatus_ != PendingStatus::WAITING_ACK) {
        return;
    }
    if (static_cast<int32_t>(nowMs - pendingDeadlineMs_) < 0) {
        return;
    }

    if (retryCount_ < kMaxRetryCount) {
        ++retryCount_;
        irSender_.sendCommand(pendingCommand_);
        logger_.log(nowMs, LogEventType::COMMAND_SENT, pendingCommand_, true);
        pendingDeadlineMs_ = nowMs + kAckTimeoutMs;
        return;
    }

    logger_.log(nowMs, LogEventType::COMMAND_DROPPED, pendingCommand_, false);
    pendingStatus_ = PendingStatus::IDLE;
    retryCount_ = 0;
    pendingCommand_ = Command::NONE;
}
