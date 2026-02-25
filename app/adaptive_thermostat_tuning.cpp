#include "adaptive_thermostat_tuning.h"

#include <cmath>

AdaptiveThermostatTuning::AdaptiveThermostatTuning() = default;

AdaptiveThermostatTuning::AdaptiveThermostatTuning(const Config& config) : config_(config) {}

void AdaptiveThermostatTuning::reset(uint32_t nowMs, float roomTempC) {
    pendingSample_ = false;
    sampleStartMs_ = nowMs;
    sampleStartTempC_ = roomTempC;
    sampleStepsSent_ = 0;
}

void AdaptiveThermostatTuning::onHeatingStepsSent(uint32_t nowMs, float roomTempC, int8_t stepsSent) {
    if (stepsSent <= 0) {
        return;
    }

    if (!pendingSample_) {
        pendingSample_ = true;
        sampleStartMs_ = nowMs;
        sampleStartTempC_ = roomTempC;
        sampleStepsSent_ = stepsSent;
        return;
    }

    // Keep one evaluation sample active and accumulate step effort conservatively.
    const int accumulated = static_cast<int>(sampleStepsSent_) + static_cast<int>(stepsSent);
    sampleStepsSent_ = clampSteps(accumulated, 1, 32);
}

AdaptiveThermostatTuning::Overrides AdaptiveThermostatTuning::update(uint32_t nowMs,
                                                                     float roomTempC,
                                                                     ThermostatMode mode,
                                                                     const ThermostatTuning& baseTuning) {
    const ModeBounds& bounds = boundsForMode(mode);

    if (pendingSample_ && (nowMs - sampleStartMs_) >= config_.evaluationWindowMs) {
        const float deltaTemp = roomTempC - sampleStartTempC_;
        const float deltaTimeSeconds = static_cast<float>(nowMs - sampleStartMs_) / 1000.0F;

        if (deltaTimeSeconds > 0.0F) {
            // Normalize by sent steps so stronger command batches do not over-bias adaptation.
            const float stepCount = static_cast<float>((sampleStepsSent_ > 0) ? sampleStepsSent_ : 1);
            const float measuredRate = (deltaTemp / deltaTimeSeconds) / stepCount;

            if (!rateAverageInitialized_) {
                heatingRateAverageCPerSec_ = measuredRate;
                rateAverageInitialized_ = true;
            } else {
                const float alpha = clampFloat(config_.movingAverageAlpha, 0.05F, 1.0F);
                heatingRateAverageCPerSec_ =
                    (alpha * measuredRate) + ((1.0F - alpha) * heatingRateAverageCPerSec_);
            }

            const bool canAdjustNow =
                (!hasAdjustedOnce_) || ((nowMs - lastAdjustmentMs_) >= config_.evaluationWindowMs);
            if (canAdjustNow && config_.expectedHeatingRateCPerSec > 0.0F) {
                const float expected = config_.expectedHeatingRateCPerSec;
                const float lower = expected * (1.0F - config_.adaptationDeadzoneRatio);
                const float upper = expected * (1.0F + config_.adaptationDeadzoneRatio);

                if (heatingRateAverageCPerSec_ < lower) {
                    aggressivenessScale_ += config_.aggressivenessStep;
                    hasAdjustedOnce_ = true;
                    lastAdjustmentMs_ = nowMs;
                } else if (heatingRateAverageCPerSec_ > upper) {
                    aggressivenessScale_ -= config_.aggressivenessStep;
                    hasAdjustedOnce_ = true;
                    lastAdjustmentMs_ = nowMs;
                }
            }
        }

        pendingSample_ = false;
        sampleStepsSent_ = 0;
    }

    const float minScaleFromKp = bounds.kpMin / ((baseTuning.kp > 0.0F) ? baseTuning.kp : 1.0F);
    const float maxScaleFromKp = bounds.kpMax / ((baseTuning.kp > 0.0F) ? baseTuning.kp : 1.0F);
    aggressivenessScale_ = clampFloat(aggressivenessScale_, minScaleFromKp, maxScaleFromKp);

    Overrides out{};
    out.kp = clampFloat(baseTuning.kp * aggressivenessScale_, bounds.kpMin, bounds.kpMax);

    const int scaledSteps =
        static_cast<int>(std::lround(static_cast<float>(baseTuning.maxSteps) * aggressivenessScale_));
    out.maxSteps = clampSteps(scaledSteps, bounds.maxStepsMin, bounds.maxStepsMax);

    return out;
}

float AdaptiveThermostatTuning::clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

int8_t AdaptiveThermostatTuning::clampSteps(int value, int8_t minValue, int8_t maxValue) {
    if (value < static_cast<int>(minValue)) {
        return minValue;
    }
    if (value > static_cast<int>(maxValue)) {
        return maxValue;
    }
    return static_cast<int8_t>(value);
}

const AdaptiveThermostatTuning::ModeBounds& AdaptiveThermostatTuning::boundsForMode(ThermostatMode mode) const {
    return (mode == ThermostatMode::FAST) ? config_.fastBounds : config_.ecoBounds;
}
