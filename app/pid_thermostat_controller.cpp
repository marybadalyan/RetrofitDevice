#include "pid_thermostat_controller.h"

#include <cmath>

PidThermostatController::PidThermostatController() = default;

PidThermostatController::PidThermostatController(const Config& config) : config_(config) {}

void PidThermostatController::setMode(ThermostatMode mode) {
    mode_ = mode;
}

ThermostatMode PidThermostatController::mode() const {
    return mode_;
}

ThermostatTuning PidThermostatController::baseTuningForMode(ThermostatMode mode) const {
    return (mode == ThermostatMode::FAST) ? config_.fast : config_.eco;
}

void PidThermostatController::setRuntimeOverrides(const RuntimeOverrides& overrides) {
    runtimeOverrides_ = overrides;
}

void PidThermostatController::clearRuntimeOverrides() {
    runtimeOverrides_ = RuntimeOverrides{};
}

void PidThermostatController::reset(float roomTempC) {
    initialized_ = true;
    controlCycleInitialized_ = false;
    lastRoomTempC_ = roomTempC;
    integral_ = 0.0F;
}

PidThermostatController::Result PidThermostatController::tick(uint32_t nowMs, float targetTempC, float roomTempC) {
    Result result{};

    if (!initialized_) {
        initialized_ = true;
        lastRoomTempC_ = roomTempC;
    }

    if (!controlCycleInitialized_) {
        controlCycleInitialized_ = true;
        lastCycleMs_ = nowMs;
    } else if ((nowMs - lastCycleMs_) < config_.controlIntervalMs) {
        return result;
    } else {
        lastCycleMs_ = nowMs;
    }

    result.ranControlCycle = true;
    result.errorC = targetTempC - roomTempC;

    if (std::fabs(result.errorC) < config_.deadbandC) {
        lastRoomTempC_ = roomTempC;
        return result;
    }

    ThermostatTuning tuning = (mode_ == ThermostatMode::FAST) ? config_.fast : config_.eco;
    if (runtimeOverrides_.enabled) {
        tuning.kp = runtimeOverrides_.kp;
        tuning.maxSteps = runtimeOverrides_.maxSteps;
    }
    const float dtSeconds = static_cast<float>(config_.controlIntervalMs) / 1000.0F;

    result.p = tuning.kp * result.errorC;

    integral_ += result.errorC * dtSeconds;
    integral_ = clampFloat(integral_, -config_.integralLimit, config_.integralLimit);
    result.i = tuning.ki * integral_;

    float derivative = 0.0F;
    if (std::fabs(result.errorC) <= config_.derivativeEnableErrorThresholdC) {
        derivative = -(roomTempC - lastRoomTempC_) / dtSeconds;
    }
    result.d = tuning.kd * derivative;

    result.output = result.p + result.i + result.d;

    int rawSteps = static_cast<int>(std::lround(result.output));
    rawSteps = static_cast<int>(clampSteps(rawSteps, static_cast<int8_t>(-tuning.maxSteps), tuning.maxSteps));
    result.steps = static_cast<int8_t>(rawSteps);

    lastRoomTempC_ = roomTempC;
    return result;
}

float PidThermostatController::clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

int8_t PidThermostatController::clampSteps(int value, int8_t minValue, int8_t maxValue) {
    if (value < static_cast<int>(minValue)) {
        return minValue;
    }
    if (value > static_cast<int>(maxValue)) {
        return maxValue;
    }
    return static_cast<int8_t>(value);
}
