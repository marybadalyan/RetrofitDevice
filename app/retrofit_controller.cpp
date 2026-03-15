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
    snapshot.mode = pidController_.mode();
    snapshot.lastTxFailure = lastTxFailure_;
    return snapshot;
}

void RetrofitController::setThermostatMode(ThermostatMode mode) {
    pidController_.setMode(mode);
    pidController_.clearRuntimeOverrides();
}

ThermostatMode RetrofitController::thermostatMode() const {
    return pidController_.mode();
}

void RetrofitController::tick(uint32_t nowMs,
                              uint32_t nowUs,
                              const WallClockSnapshot& wallNow,
                              float roomTemperatureC) {
    (void)nowUs;

    Command next = Command::NONE;
    LogEventType sourceType = LogEventType::COMMAND_DROPPED;
    if (chooseNextCommand(nowMs, wallNow, next, sourceType)) {
        const bool applied = applyThermostatControlCommand(next);
        logger_.log(wallNow, sourceType, next, applied);
    }

    runThermostatLoop(nowMs, wallNow, roomTemperatureC);
}

bool RetrofitController::chooseNextCommand(uint32_t nowMs,
                                           const WallClockSnapshot& wallNow,
                                           Command& outCommand,
                                           LogEventType& sourceType) {
    // Hub command has immediate priority for this tick; scheduled command remains pending for later ticks.
    if (hubReceiver_.poll(outCommand)) {
        sourceType = LogEventType::HUB_COMMAND_RX;
        return true;
    }

    if (scheduler_.enabled()) {
        if (scheduler_.nextDueCommand(nowMs, wallNow, outCommand)) {
            sourceType = LogEventType::SCHEDULE_COMMAND;
            return true;
        }
        return false;
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

void RetrofitController::runThermostatLoop(uint32_t nowMs,
                                            const WallClockSnapshot& wallNow,
                                            float roomTemperatureC) {
    if (!powerEnabled_) {
        pidController_.reset(roomTemperatureC);
        pidController_.clearRuntimeOverrides();
        adaptiveTuning_.reset(nowMs, roomTemperatureC);
        return;
    }

    const ThermostatMode currentMode = pidController_.mode();
    const ThermostatTuning baseTuning = pidController_.baseTuningForMode(currentMode);
    const AdaptiveThermostatTuning::Overrides adaptiveOverrides =
        adaptiveTuning_.update(nowMs, roomTemperatureC, currentMode, baseTuning);

    PidThermostatController::RuntimeOverrides runtimeOverrides{};
    runtimeOverrides.enabled = true;
    runtimeOverrides.kp = adaptiveOverrides.kp;
    runtimeOverrides.maxSteps = adaptiveOverrides.maxSteps;
    pidController_.setRuntimeOverrides(runtimeOverrides);

    const PidThermostatController::Result pidResult = pidController_.tick(nowMs, targetTemperatureC_, roomTemperatureC);
    if (!pidResult.ranControlCycle || pidResult.steps == 0) {
        return;
    }

    const Command stepCommand = (pidResult.steps > 0) ? Command::TEMP_UP : Command::TEMP_DOWN;
    const int8_t stepCount = static_cast<int8_t>((pidResult.steps > 0) ? pidResult.steps : -pidResult.steps);
    adaptiveTuning_.onControlStepsSent(nowMs, roomTemperatureC, pidResult.steps);

    for (int8_t i = 0; i < stepCount; ++i) {
        sendCommand(stepCommand, wallNow, LogEventType::THERMOSTAT_CONTROL);
    }
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
    if (command == Command::ON) {
        heaterCommandedOn_ = true;
    } else if (command == Command::OFF) {
        heaterCommandedOn_ = false;
    }
}
