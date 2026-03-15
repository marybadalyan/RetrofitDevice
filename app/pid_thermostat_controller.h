#pragma once

#include <cstdint>

enum class ThermostatMode : uint8_t {
    FAST = 0,
    ECO = 1,
};

struct ThermostatTuning {
    float kp;
    float ki;
    float kd;
    int8_t maxSteps;
};

class PidThermostatController {
public:
    struct Config {
        // Control loop period in milliseconds.
        uint32_t controlIntervalMs = 45000U;
        // Anti-windup clamp for integral accumulator.
        float integralLimit = 50.0F;
        // Skip IR command when close enough to target.
        float deadbandC = 0.3F;
        // Disable derivative term in boost phase.
        float derivativeEnableErrorThresholdC = 2.0F;
        ThermostatTuning fast{1.6F, 0.02F, 3.0F, 3};
        ThermostatTuning eco{0.9F, 0.01F, 2.0F, 2};
    };

    struct Result {
        bool ranControlCycle = false;
        float errorC = 0.0F;
        float p = 0.0F;
        float i = 0.0F;
        float d = 0.0F;
        float output = 0.0F;
        int8_t steps = 0;
    };

    struct RuntimeOverrides {
        bool enabled = false;
        float kp = 0.0F;
        int8_t maxSteps = 1;
    };

    PidThermostatController();
    explicit PidThermostatController(const Config& config);

    void setMode(ThermostatMode mode);
    ThermostatMode mode() const;
    ThermostatTuning baseTuningForMode(ThermostatMode mode) const;
    void setRuntimeOverrides(const RuntimeOverrides& overrides);
    void clearRuntimeOverrides();

    void reset(float roomTempC);
    Result tick(uint32_t nowMs, float targetTempC, float roomTempC);

private:
    static float clampFloat(float value, float minValue, float maxValue);
    static int8_t clampSteps(int value, int8_t minValue, int8_t maxValue);

    Config config_{};
    ThermostatMode mode_ = ThermostatMode::FAST;
    bool initialized_ = false;
    bool controlCycleInitialized_ = false;
    uint32_t lastCycleMs_ = 0U;
    float lastRoomTempC_ = 0.0F;
    float integral_ = 0.0F;
    RuntimeOverrides runtimeOverrides_{};
};
