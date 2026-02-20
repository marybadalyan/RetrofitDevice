#pragma once

#include <cstdint>

#include "../IRReciever.h"
#include "../IRSender.h"
#include "../commands.h"
#include "../hub/hub_receiver.h"
#include "../logger.h"
#include "../scheduler/scheduler.h"
#include "../time/wall_clock.h"

class RetrofitController {
public:
    struct HealthSnapshot {
        bool powerEnabled = false;
        bool heaterCommandedOn = false;
        float targetTemperatureC = 0.0F;
        bool waitingAck = false;
        uint8_t retryCount = 0;
        Command pendingCommand = Command::NONE;
        TxFailureCode lastTxFailure = TxFailureCode::NONE;
    };

    RetrofitController(IRSender& irSender,
                       IRReceiver& irReceiver,
                       HubReceiver& hubReceiver,
                       CommandScheduler& scheduler,
                       Logger& logger);

    void begin(bool schedulerEnabled);
    void tick(uint32_t nowMs, uint32_t nowUs, const WallClockSnapshot& wallNow, float roomTemperatureC);
    HealthSnapshot healthSnapshot() const;

private:
    enum class PendingStatus : uint8_t { IDLE = 0, WAITING_ACK = 1 };

    void processIncomingFrames(const WallClockSnapshot& wallNow);
    bool chooseNextCommand(uint32_t nowMs,
                           const WallClockSnapshot& wallNow,
                           Command& outCommand,
                           LogEventType& sourceType);
    bool applyThermostatControlCommand(Command command);
    bool shouldHeat(float roomTemperatureC) const;
    void runThermostatLoop(const WallClockSnapshot& wallNow, float roomTemperatureC);
    void sendCommand(Command command, const WallClockSnapshot& wallNow, LogEventType sourceType);
    void handlePendingTimeout(const WallClockSnapshot& wallNow);

    IRSender& irSender_;
    IRReceiver& irReceiver_;
    HubReceiver& hubReceiver_;
    CommandScheduler& scheduler_;
    Logger& logger_;

    PendingStatus pendingStatus_ = PendingStatus::IDLE;
    Command pendingCommand_ = Command::NONE;
    uint32_t pendingDeadlineMs_ = 0;
    uint8_t retryCount_ = 0;
    bool powerEnabled_ = false;
    bool heaterCommandedOn_ = false;
    float targetTemperatureC_ = 22.0F;
    TxFailureCode lastTxFailure_ = TxFailureCode::NONE;
};
