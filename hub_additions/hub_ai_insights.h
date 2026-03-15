#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class HubAiInsights {
public:
    enum class CommandSent : uint8_t {
        NONE = 0,
        HEAT_UP = 1,
        HEAT_DOWN = 2,
    };

    enum class Mode : uint8_t {
        COMFORT = 0,
        ECO = 1,
        BOOST = 2,
    };

    struct LogEntry {
        uint64_t timestampMs = 0;
        float roomTemperatureC = 0.0F;
        float targetTemperatureC = 0.0F;
        CommandSent commandSent = CommandSent::NONE;
        float pidOutput = 0.0F;
        Mode mode = Mode::COMFORT;
    };

    struct SystemInsights {
        float heating_rate = 0.0F;
        float cooling_rate = 0.0F;
        float kp_scale = 1.0F;
        float ki_scale = 1.0F;
        bool overshoot_detected = false;
        bool oscillation_detected = false;
        bool window_open_detected = false;
        bool hardware_failure_detected = false;
        float confidence_score = 0.0F;
    };

    struct PidRecommendation {
        float kpScale = 1.0F;
        float kiScale = 1.0F;
        bool kdAutoChange = false;
        bool advisoryOnly = true;
        bool requiresUserIntent = true;
    };

    struct Config {
        uint32_t thermalWindowMs = 7U * 60U * 1000U;
        uint32_t heaterActiveHoldMs = 10U * 60U * 1000U;
        float emaAlpha = 0.20F;
        float minTempNoiseC = 0.05F;
        float overshootThresholdC = 1.5F;
        uint8_t overshootRepeatThreshold = 2;
        uint8_t oscillationCrossingsPerHourThreshold = 6;
        float slowResponseFactor = 1.40F;
        float minimumLearnedHeatingRateCPerSec = 0.0002F;
        float steadyStateErrorThresholdC = 0.6F;
        uint16_t steadyStatePersistenceSamples = 8;
        uint16_t hardwareFailureThreshold = 3;
        uint8_t minHeatUpCommandsForFailureEval = 2;
        float failureNoIncreaseThresholdC = 0.2F;
        float normalHeatingResumeThresholdC = 0.4F;
        float windowOpenDropRateMultiplier = 3.0F;
    };

    HubAiInsights();
    explicit HubAiInsights(const Config& config);

    void reset();
    void ingest(const LogEntry& entry);

    const SystemInsights& insights() const;
    PidRecommendation recommendation() const;
    const char* probableHardwareCause() const;

private:
    static float clampFloat(float value, float minValue, float maxValue);
    static float absFloat(float value);
    static int8_t errorSign(float errorC);
    static float updateEma(float alpha, float sample, float current, bool& initializedFlag);

    void evaluateHeatingWindowIfReady(const LogEntry& entry);
    void updateCoolingRate(const LogEntry& entry, float dtSec, float dTempC);
    void updateControlQuality(const LogEntry& entry, float dtSec, float dTempC);
    void updateWindowOpenDetection(float dtSec, float dTempC);
    void startHeatingWindow(const LogEntry& entry);
    void registerHeatUpCommand(const LogEntry& entry);
    void updateRecommendations(const LogEntry& entry);
    void updateConfidenceScore();
    void pushCrossing(uint64_t timestampMs);
    size_t crossingsInLastHour(uint64_t nowMs) const;

    Config config_{};
    bool hasPrevious_ = false;
    LogEntry previous_{};
    size_t ingestedSamples_ = 0;

    bool heatingRateInitialized_ = false;
    bool coolingRateInitialized_ = false;
    float heatingRateEmaCPerSec_ = 0.0F;
    float coolingRateEmaCPerSec_ = 0.0F;

    bool heatingWindowActive_ = false;
    uint64_t heatingWindowStartMs_ = 0;
    float heatingWindowStartTempC_ = 0.0F;
    uint8_t heatingWindowHeatUpCommands_ = 0;

    bool responseTrackingActive_ = false;
    uint64_t responseStartMs_ = 0;
    float responseStartTempC_ = 0.0F;
    float responseStartTargetC_ = 0.0F;
    bool responseSlowFlagged_ = false;
    uint32_t slowResponseCount_ = 0;

    bool previousOvershootSample_ = false;
    uint32_t overshootCount_ = 0;
    int8_t previousErrorSign_ = 0;

    static constexpr size_t kCrossingHistoryCapacity = 64;
    std::array<uint64_t, kCrossingHistoryCapacity> crossingHistoryMs_{};
    size_t crossingHead_ = 0;
    size_t crossingCount_ = 0;

    uint16_t persistentErrorSamples_ = 0;
    bool steadyStateErrorPersistent_ = false;

    uint64_t heaterActiveUntilMs_ = 0;
    uint16_t hardwareFailureCounter_ = 0;
    bool hardwareFailureDetected_ = false;

    uint32_t ecoSamples_ = 0;
    uint32_t ecoInBandSamples_ = 0;
    uint32_t ecoHeatUpCommands_ = 0;

    float kpScaleComfort_ = 1.0F;
    float kpScaleEco_ = 0.95F;
    float kiScale_ = 1.0F;

    SystemInsights insights_{};
};
