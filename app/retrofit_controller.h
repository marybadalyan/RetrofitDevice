#pragma once

#include <cstdint>

#include "../IRReciever.h"
#include "../IRSender.h"
#include "../commands.h"
#include "../hub/hub_receiver.h"
#include "../logger.h"
#include "../scheduler/scheduler.h"

class RetrofitController {
public:
    RetrofitController(IRSender& irSender,
                       IRReceiver& irReceiver,
                       HubReceiver& hubReceiver,
                       CommandScheduler& scheduler,
                       Logger& logger);

    void begin(bool schedulerEnabled);
    void tick(uint32_t nowMs);

private:
    enum class PendingStatus : uint8_t { IDLE = 0, WAITING_ACK = 1 };

    void processIncomingFrames(uint32_t nowMs);
    bool chooseNextCommand(uint32_t nowMs, Command& outCommand, LogEventType& sourceType);
    void sendCommand(Command command, uint32_t nowMs, LogEventType sourceType);
    void handlePendingTimeout(uint32_t nowMs);

    IRSender& irSender_;
    IRReceiver& irReceiver_;
    HubReceiver& hubReceiver_;
    CommandScheduler& scheduler_;
    Logger& logger_;

    PendingStatus pendingStatus_ = PendingStatus::IDLE;
    Command pendingCommand_ = Command::NONE;
    uint32_t pendingDeadlineMs_ = 0;
    uint8_t retryCount_ = 0;
};
