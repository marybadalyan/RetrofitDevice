#pragma once

#include <cstdint>

#include "pid_thermostat_controller.h"

class AdaptiveThermostatTuning {
public:
    struct ModeBounds {
        float kpMin;
        float kpMax;
        int8_t maxStepsMin;
        int8_t maxStepsMax;
    };

    struct Config {
        // Minimum time after a heating command before response is evaluated.
        uint32_t evaluationWindowMs = 420000U;  // 7 minutes
        // Target room heating rate in C/s.
        float expectedHeatingRateCPerSec = 0.0008F;
        // Ignore small mismatch from expected rate to reduce control noise.
        float adaptationDeadzoneRatio = 0.20F;
        // Conservative aggressiveness adjustment size per evaluation.
        float aggressivenessStep = 0.05F;
        // Exponential moving-average smoothing factor for measured heating rate.
        float movingAverageAlpha = 0.25F;
        ModeBounds fastBounds{1.0F, 2.2F, 1, 4};
        ModeBounds ecoBounds{0.6F, 1.4F, 1, 3};
    };

    struct Overrides {
        float kp = 0.0F;
        int8_t maxSteps = 1;
    };

    AdaptiveThermostatTuning();
    explicit AdaptiveThermostatTuning(const Config& config);

    void reset(uint32_t nowMs, float roomTempC);
    void onHeatingStepsSent(uint32_t nowMs, float roomTempC, int8_t stepsSent);
    Overrides update(uint32_t nowMs, float roomTempC, ThermostatMode mode, const ThermostatTuning& baseTuning);

private:
    static float clampFloat(float value, float minValue, float maxValue);
    static int8_t clampSteps(int value, int8_t minValue, int8_t maxValue);
    const ModeBounds& boundsForMode(ThermostatMode mode) const;

    Config config_{};

    bool rateAverageInitialized_ = false;
    float heatingRateAverageCPerSec_ = 0.0F;

    bool pendingSample_ = false;
    uint32_t sampleStartMs_ = 0U;
    float sampleStartTempC_ = 0.0F;
    int8_t sampleStepsSent_ = 0;

    uint32_t lastAdjustmentMs_ = 0U;
    bool hasAdjustedOnce_ = false;

    float aggressivenessScale_ = 1.0F;
};
