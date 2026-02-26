#include "hub_ai_insights.h"

namespace {
constexpr uint64_t kOneHourMs = 60ULL * 60ULL * 1000ULL;
constexpr float kTargetReachedToleranceC = 0.20F;
constexpr float kCrossingDeadbandC = 0.10F;
}  // namespace

HubAiInsights::HubAiInsights() = default;

HubAiInsights::HubAiInsights(const Config& config) : config_(config) {}

void HubAiInsights::reset() {
    hasPrevious_ = false;
    previous_ = LogEntry{};
    ingestedSamples_ = 0;

    heatingRateInitialized_ = false;
    coolingRateInitialized_ = false;
    heatingRateEmaCPerSec_ = 0.0F;
    coolingRateEmaCPerSec_ = 0.0F;

    heatingWindowActive_ = false;
    heatingWindowStartMs_ = 0;
    heatingWindowStartTempC_ = 0.0F;
    heatingWindowHeatUpCommands_ = 0;

    responseTrackingActive_ = false;
    responseStartMs_ = 0;
    responseStartTempC_ = 0.0F;
    responseStartTargetC_ = 0.0F;
    responseSlowFlagged_ = false;
    slowResponseCount_ = 0;

    previousOvershootSample_ = false;
    overshootCount_ = 0;
    previousErrorSign_ = 0;

    crossingHistoryMs_.fill(0);
    crossingHead_ = 0;
    crossingCount_ = 0;

    persistentErrorSamples_ = 0;
    steadyStateErrorPersistent_ = false;

    heaterActiveUntilMs_ = 0;
    hardwareFailureCounter_ = 0;
    hardwareFailureDetected_ = false;

    ecoSamples_ = 0;
    ecoInBandSamples_ = 0;
    ecoHeatUpCommands_ = 0;

    kpScaleComfort_ = 1.0F;
    kpScaleEco_ = 0.95F;
    kiScale_ = 1.0F;

    insights_ = SystemInsights{};
}

void HubAiInsights::ingest(const LogEntry& entry) {
    if (hasPrevious_ && entry.timestampMs <= previous_.timestampMs) {
        return;
    }

    evaluateHeatingWindowIfReady(entry);

    if (hasPrevious_) {
        const float dtSec = static_cast<float>(entry.timestampMs - previous_.timestampMs) / 1000.0F;
        const float dTempC = entry.roomTemperatureC - previous_.roomTemperatureC;
        if (dtSec > 0.0F) {
            updateCoolingRate(entry, dtSec, dTempC);
            updateControlQuality(entry, dtSec, dTempC);
            updateWindowOpenDetection(dtSec, dTempC);
        }
    } else {
        previousErrorSign_ = errorSign(entry.targetTemperatureC - entry.roomTemperatureC);
    }

    registerHeatUpCommand(entry);

    if (entry.mode == Mode::ECO) {
        ++ecoSamples_;
        const float absError = absFloat(entry.roomTemperatureC - entry.targetTemperatureC);
        if (absError <= 1.0F) {
            ++ecoInBandSamples_;
        }
        if (entry.commandSent == CommandSent::HEAT_UP) {
            ++ecoHeatUpCommands_;
        }
    }

    updateRecommendations(entry);
    updateConfidenceScore();

    previous_ = entry;
    hasPrevious_ = true;
    ++ingestedSamples_;
}

const HubAiInsights::SystemInsights& HubAiInsights::insights() const {
    return insights_;
}

HubAiInsights::PidRecommendation HubAiInsights::recommendation() const {
    PidRecommendation out{};
    out.kpScale = insights_.kp_scale;
    out.kiScale = insights_.ki_scale;
    return out;
}

const char* HubAiInsights::probableHardwareCause() const {
    if (!hardwareFailureDetected_) {
        return "";
    }
    return "IR transmitter failure, blocked heater receiver, heater powered off, or wrong IR code";
}

float HubAiInsights::clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

float HubAiInsights::absFloat(float value) {
    return (value < 0.0F) ? -value : value;
}

int8_t HubAiInsights::errorSign(float errorC) {
    if (errorC > kCrossingDeadbandC) {
        return 1;
    }
    if (errorC < -kCrossingDeadbandC) {
        return -1;
    }
    return 0;
}

float HubAiInsights::updateEma(float alpha, float sample, float current, bool& initializedFlag) {
    const float boundedAlpha = clampFloat(alpha, 0.01F, 1.0F);
    if (!initializedFlag) {
        initializedFlag = true;
        return sample;
    }
    return (boundedAlpha * sample) + ((1.0F - boundedAlpha) * current);
}

void HubAiInsights::evaluateHeatingWindowIfReady(const LogEntry& entry) {
    if (!heatingWindowActive_) {
        return;
    }

    const uint64_t elapsedMs = entry.timestampMs - heatingWindowStartMs_;
    if (elapsedMs < static_cast<uint64_t>(config_.thermalWindowMs)) {
        return;
    }

    const float elapsedSec = static_cast<float>(elapsedMs) / 1000.0F;
    if (elapsedSec <= 0.0F) {
        heatingWindowActive_ = false;
        heatingWindowHeatUpCommands_ = 0;
        return;
    }

    const float deltaTempC = entry.roomTemperatureC - heatingWindowStartTempC_;
    const float measuredHeatingRate = deltaTempC / elapsedSec;
    if (measuredHeatingRate > 0.0F) {
        heatingRateEmaCPerSec_ =
            updateEma(config_.emaAlpha, measuredHeatingRate, heatingRateEmaCPerSec_, heatingRateInitialized_);
    }

    if (heatingWindowHeatUpCommands_ >= config_.minHeatUpCommandsForFailureEval) {
        if (deltaTempC <= config_.failureNoIncreaseThresholdC) {
            ++hardwareFailureCounter_;
            if (hardwareFailureCounter_ >= config_.hardwareFailureThreshold) {
                hardwareFailureDetected_ = true;
            }
        } else if (deltaTempC >= config_.normalHeatingResumeThresholdC) {
            hardwareFailureCounter_ = 0;
            hardwareFailureDetected_ = false;
        }
    }

    heatingWindowActive_ = false;
    heatingWindowHeatUpCommands_ = 0;
}

void HubAiInsights::updateCoolingRate(const LogEntry& entry, float dtSec, float dTempC) {
    if (dtSec <= 0.0F) {
        return;
    }

    const bool heaterActive = (entry.timestampMs <= heaterActiveUntilMs_);
    if (heaterActive) {
        return;
    }
    if (dTempC >= -config_.minTempNoiseC) {
        return;
    }

    const float coolingMagnitude = (-dTempC) / dtSec;
    coolingRateEmaCPerSec_ =
        updateEma(config_.emaAlpha, coolingMagnitude, coolingRateEmaCPerSec_, coolingRateInitialized_);
}

void HubAiInsights::updateControlQuality(const LogEntry& entry, float dtSec, float dTempC) {
    (void)dtSec;

    const float errorC = entry.targetTemperatureC - entry.roomTemperatureC;

    const bool overshootSample = ((entry.roomTemperatureC - entry.targetTemperatureC) > config_.overshootThresholdC);
    if (overshootSample && !previousOvershootSample_) {
        ++overshootCount_;
    }
    previousOvershootSample_ = overshootSample;
    insights_.overshoot_detected = (overshootCount_ >= config_.overshootRepeatThreshold);

    const int8_t signNow = errorSign(errorC);
    if (previousErrorSign_ != 0 && signNow != 0 && signNow != previousErrorSign_) {
        pushCrossing(entry.timestampMs);
    }
    if (signNow != 0) {
        previousErrorSign_ = signNow;
    }
    insights_.oscillation_detected =
        (crossingsInLastHour(entry.timestampMs) > config_.oscillationCrossingsPerHourThreshold);

    if (absFloat(errorC) > config_.steadyStateErrorThresholdC && absFloat(dTempC) <= config_.minTempNoiseC) {
        if (persistentErrorSamples_ < 65535U) {
            ++persistentErrorSamples_;
        }
    } else if (persistentErrorSamples_ > 0U) {
        --persistentErrorSamples_;
    }
    steadyStateErrorPersistent_ = (persistentErrorSamples_ >= config_.steadyStatePersistenceSamples);

    if (!responseTrackingActive_) {
        if (entry.commandSent == CommandSent::HEAT_UP && errorC > 0.5F) {
            responseTrackingActive_ = true;
            responseStartMs_ = entry.timestampMs;
            responseStartTempC_ = entry.roomTemperatureC;
            responseStartTargetC_ = entry.targetTemperatureC;
            responseSlowFlagged_ = false;
        }
        return;
    }

    const float learnedRate =
        heatingRateInitialized_ ? heatingRateEmaCPerSec_ : config_.minimumLearnedHeatingRateCPerSec;
    const float baselineRate = (learnedRate > config_.minimumLearnedHeatingRateCPerSec)
                                   ? learnedRate
                                   : config_.minimumLearnedHeatingRateCPerSec;

    const float initialErrorC = responseStartTargetC_ - responseStartTempC_;
    const float expectedSec = (initialErrorC > 0.2F) ? (initialErrorC / baselineRate) : 1.0F;
    const float elapsedSec = static_cast<float>(entry.timestampMs - responseStartMs_) / 1000.0F;

    if (!responseSlowFlagged_ && elapsedSec > (expectedSec * config_.slowResponseFactor) &&
        entry.roomTemperatureC < (entry.targetTemperatureC - kTargetReachedToleranceC)) {
        ++slowResponseCount_;
        responseSlowFlagged_ = true;
    }

    if (entry.roomTemperatureC >= (entry.targetTemperatureC - kTargetReachedToleranceC)) {
        if (elapsedSec > (expectedSec * config_.slowResponseFactor) && !responseSlowFlagged_) {
            ++slowResponseCount_;
        }
        responseTrackingActive_ = false;
        responseSlowFlagged_ = false;
    }
}

void HubAiInsights::updateWindowOpenDetection(float dtSec, float dTempC) {
    if (dtSec <= 0.0F || !coolingRateInitialized_) {
        return;
    }
    if (previous_.timestampMs > heaterActiveUntilMs_) {
        return;
    }
    if (dTempC >= -config_.minTempNoiseC) {
        return;
    }

    const float dropRate = (-dTempC) / dtSec;
    if (dropRate > (coolingRateEmaCPerSec_ * config_.windowOpenDropRateMultiplier)) {
        insights_.window_open_detected = true;
    }
}

void HubAiInsights::startHeatingWindow(const LogEntry& entry) {
    heatingWindowActive_ = true;
    heatingWindowStartMs_ = entry.timestampMs;
    heatingWindowStartTempC_ = entry.roomTemperatureC;
    heatingWindowHeatUpCommands_ = 1;
}

void HubAiInsights::registerHeatUpCommand(const LogEntry& entry) {
    if (entry.commandSent != CommandSent::HEAT_UP) {
        return;
    }

    heaterActiveUntilMs_ = entry.timestampMs + static_cast<uint64_t>(config_.heaterActiveHoldMs);

    if (!heatingWindowActive_) {
        startHeatingWindow(entry);
        return;
    }

    if (heatingWindowHeatUpCommands_ < 255U) {
        ++heatingWindowHeatUpCommands_;
    }
}

void HubAiInsights::updateRecommendations(const LogEntry& entry) {
    float kpComfort = 1.0F;
    float ki = 1.0F;

    if (slowResponseCount_ > 0U) {
        kpComfort += 0.05F;
    }
    if (insights_.overshoot_detected) {
        kpComfort -= 0.08F;
    }
    if (insights_.oscillation_detected) {
        kpComfort -= 0.07F;
    }
    if (steadyStateErrorPersistent_) {
        ki += 0.02F;
    }

    kpComfort = clampFloat(kpComfort, 0.7F, 1.3F);
    ki = clampFloat(ki, 0.9F, 1.1F);

    float kpEco = kpComfort;
    if (ecoSamples_ > 0U) {
        const float comfortRatio = static_cast<float>(ecoInBandSamples_) / static_cast<float>(ecoSamples_);
        const float heatDemand = static_cast<float>(ecoHeatUpCommands_) / static_cast<float>(ecoSamples_);
        if (comfortRatio > 0.80F && heatDemand < 0.25F) {
            kpEco -= 0.08F;
        } else {
            kpEco -= 0.05F;
        }
    } else {
        kpEco -= 0.05F;
    }
    if (kpEco >= kpComfort) {
        kpEco = kpComfort - 0.01F;
    }
    kpEco = clampFloat(kpEco, 0.7F, 1.3F);

    kpScaleComfort_ = kpComfort;
    kpScaleEco_ = kpEco;
    kiScale_ = ki;

    insights_.heating_rate = heatingRateEmaCPerSec_;
    insights_.cooling_rate = coolingRateEmaCPerSec_;
    insights_.kp_scale = (entry.mode == Mode::ECO) ? kpScaleEco_ : kpScaleComfort_;
    insights_.ki_scale = kiScale_;
    insights_.hardware_failure_detected = hardwareFailureDetected_;
}

void HubAiInsights::updateConfidenceScore() {
    float confidence = 0.0F;
    if (heatingRateInitialized_) {
        confidence += 0.35F;
    }
    if (coolingRateInitialized_) {
        confidence += 0.25F;
    }

    const float sampleComponent = static_cast<float>((ingestedSamples_ >= 200U) ? 200U : ingestedSamples_) / 200.0F;
    confidence += (sampleComponent * 0.30F);

    if (insights_.overshoot_detected || insights_.oscillation_detected || insights_.hardware_failure_detected) {
        confidence += 0.10F;
    }

    insights_.confidence_score = clampFloat(confidence, 0.0F, 1.0F);
}

void HubAiInsights::pushCrossing(uint64_t timestampMs) {
    const size_t index = (crossingHead_ + crossingCount_) % kCrossingHistoryCapacity;
    crossingHistoryMs_[index] = timestampMs;
    if (crossingCount_ < kCrossingHistoryCapacity) {
        ++crossingCount_;
    } else {
        crossingHead_ = (crossingHead_ + 1U) % kCrossingHistoryCapacity;
    }
}

size_t HubAiInsights::crossingsInLastHour(uint64_t nowMs) const {
    size_t count = 0;
    for (size_t i = 0; i < crossingCount_; ++i) {
        const size_t index = (crossingHead_ + i) % kCrossingHistoryCapacity;
        const uint64_t ts = crossingHistoryMs_[index];
        if (nowMs >= ts && (nowMs - ts) <= kOneHourMs) {
            ++count;
        }
    }
    return count;
}
