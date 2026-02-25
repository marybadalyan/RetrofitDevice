#pragma once

#include <cstdint>

#include "../IRReciever.h"
#include "../IRSender.h"
#include "../commands.h"
#include "../hub/hub_receiver.h"
#include "../logger.h"
#include "../scheduler/scheduler.h"
#include "../time/wall_clock.h"
#include "adaptive_thermostat_tuning.h"
#include "pid_thermostat_controller.h"

class RetrofitController {
public:
    struct HealthSnapshot {
        bool powerEnabled = false;
        bool heaterCommandedOn = false;
        float targetTemperatureC = 0.0F;
        ThermostatMode mode = ThermostatMode::FAST;
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
    void setThermostatMode(ThermostatMode mode);
    ThermostatMode thermostatMode() const;

private:
    bool chooseNextCommand(uint32_t nowMs,
                           const WallClockSnapshot& wallNow,
                           Command& outCommand,
                           LogEventType& sourceType);
    bool applyThermostatControlCommand(Command command);
    void runThermostatLoop(uint32_t nowMs, const WallClockSnapshot& wallNow, float roomTemperatureC);
    void sendCommand(Command command, const WallClockSnapshot& wallNow, LogEventType sourceType);

    IRSender& irSender_;
    IRReceiver& irReceiver_;
    HubReceiver& hubReceiver_;
    CommandScheduler& scheduler_;
    Logger& logger_;

    bool powerEnabled_ = false;
    bool heaterCommandedOn_ = false;
    float targetTemperatureC_ = 22.0F;
    PidThermostatController pidController_{};
    AdaptiveThermostatTuning adaptiveTuning_{};
    TxFailureCode lastTxFailure_ = TxFailureCode::NONE;
};
