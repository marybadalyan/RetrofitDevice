#include <unity.h>
#include <cstdio>
#include <ctime>

#include "IRSender.h"
#define private public
#include "IRReciever.h"
#include "app/adaptive_thermostat_tuning.h"
#include "app/retrofit_controller.h"
#undef private
#include "heater/heater.h"
#include "hub/hub_ai_insights.h"
#include "hub/hub_mock_scheduler.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "scheduler/scheduler.h"
#include "time/mock_clock.h"

namespace {

WallClockSnapshot makeWall(uint32_t dateKey,
                           uint8_t weekday,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second,
                           bool valid = true) {
    WallClockSnapshot snapshot{};
    snapshot.valid = valid;
    snapshot.dateKey = dateKey;
    snapshot.weekday = weekday;
    snapshot.hour = hour;
    snapshot.minute = minute;
    snapshot.second = second;
    snapshot.secondsOfDay =
        (static_cast<uint32_t>(hour) * 3600UL) + (static_cast<uint32_t>(minute) * 60UL) + static_cast<uint32_t>(second);
    return snapshot;
}

const char* eventToString(LogEventType type) {
    switch (type) {
        case LogEventType::COMMAND_SENT:
            return "COMMAND_SENT";
        case LogEventType::COMMAND_DROPPED:
            return "COMMAND_DROPPED";
        case LogEventType::HUB_COMMAND_RX:
            return "HUB_COMMAND_RX";
        case LogEventType::SCHEDULE_COMMAND:
            return "SCHEDULE_COMMAND";
        case LogEventType::STATE_CHANGE:
            return "STATE_CHANGE";
        case LogEventType::THERMOSTAT_CONTROL:
            return "THERMOSTAT_CONTROL";
        case LogEventType::TRANSMIT_FAILED:
            return "TRANSMIT_FAILED";
        case LogEventType::IR_FRAME_RX:
            return "IR_FRAME_RX";
        default:
            return "UNKNOWN";
    }
}

void printTimeline(const Logger& logger, const char* label = "TIMELINE") {
    std::printf("\n[%s] ---- BEGIN ----\n", label);
    for (size_t i = 0; i < logger.size(); ++i) {
        const LogEntry& e = logger.entries()[i];
        std::printf("[%s] %u %02u:%02u:%02u %-17s %-9s success=%u code=%u\n",
                    label,
                    e.dateKey,
                    e.hour,
                    e.minute,
                    e.second,
                    eventToString(e.type),
                    commandToString(e.command),
                    static_cast<unsigned>(e.success),
                    static_cast<unsigned>(e.detailCode));
    }
    std::printf("[%s] ---- END ----\n\n", label);
}

WallClockSnapshot hostLocalNow(uint32_t bootMs, uint32_t bootUs) {
    WallClockSnapshot out{};
    out.bootMs = bootMs;
    out.bootUs = bootUs;

    const std::time_t unixSec = std::time(nullptr);
    if (unixSec <= 0) {
        return out;
    }

    std::tm localTm{};
#if defined(_WIN32)
    if (localtime_s(&localTm, &unixSec) != 0) {
        return out;
    }
#else
    if (localtime_r(&unixSec, &localTm) == nullptr) {
        return out;
    }
#endif

    out.valid = true;
    out.unixMs = static_cast<uint64_t>(unixSec) * 1000ULL;
    out.year = static_cast<uint16_t>(localTm.tm_year + 1900);
    out.month = static_cast<uint8_t>(localTm.tm_mon + 1);
    out.day = static_cast<uint8_t>(localTm.tm_mday);
    out.hour = static_cast<uint8_t>(localTm.tm_hour);
    out.minute = static_cast<uint8_t>(localTm.tm_min);
    out.second = static_cast<uint8_t>(localTm.tm_sec);
    out.weekday = static_cast<uint8_t>(localTm.tm_wday);
    out.secondsOfDay =
        (static_cast<uint32_t>(out.hour) * 3600UL) + (static_cast<uint32_t>(out.minute) * 60UL) + out.second;
    out.dateKey = (static_cast<uint32_t>(out.year) * 10000UL) +
                  (static_cast<uint32_t>(out.month) * 100UL) + static_cast<uint32_t>(out.day);
    return out;
}

void secondsToHms(uint32_t secondsOfDay, uint8_t& hour, uint8_t& minute, uint8_t& second) {
    const uint32_t bounded = secondsOfDay % 86400U;
    hour = static_cast<uint8_t>(bounded / 3600U);
    minute = static_cast<uint8_t>((bounded % 3600U) / 60U);
    second = static_cast<uint8_t>(bounded % 60U);
}

WallClockSnapshot hostLocalWithSecondOffset(uint32_t bootMs, uint32_t bootUs, uint32_t offsetSec) {
    WallClockSnapshot wall = hostLocalNow(bootMs, bootUs);
    if (!wall.valid) {
        return makeWall(20260223, 2, 7, 0, static_cast<uint8_t>(offsetSec % 60U), true);
    }

    uint8_t hour = wall.hour;
    uint8_t minute = wall.minute;
    uint8_t second = wall.second;
    secondsToHms(wall.secondsOfDay + offsetSec, hour, minute, second);
    wall.hour = hour;
    wall.minute = minute;
    wall.second = second;
    wall.secondsOfDay =
        (static_cast<uint32_t>(wall.hour) * 3600UL) + (static_cast<uint32_t>(wall.minute) * 60UL) + wall.second;
    return wall;
}

void test_scheduler_relative_due() {
    CommandScheduler scheduler;
    scheduler.setEnabled(true);

    TEST_ASSERT_TRUE(scheduler.addEntry(1000, Command::ON));

    Command out = Command::NONE;
    const WallClockSnapshot noWall{};
    TEST_ASSERT_FALSE(scheduler.nextDueCommand(999, noWall, out));
    TEST_ASSERT_TRUE(scheduler.nextDueCommand(1000, noWall, out));
    TEST_ASSERT_EQUAL(Command::ON, out);
}

void test_scheduler_daily_once_per_day() {
    CommandScheduler scheduler;
    scheduler.setEnabled(true);
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(8, 0, 0, Command::ON, kWeekdayWeekdays));

    Command out = Command::NONE;
    const WallClockSnapshot mondayAt8 = makeWall(20260223, 1, 8, 0, 0, true);

    TEST_ASSERT_TRUE(scheduler.nextDueCommand(0, mondayAt8, out));
    TEST_ASSERT_EQUAL(Command::ON, out);

    out = Command::NONE;
    TEST_ASSERT_FALSE(scheduler.nextDueCommand(0, mondayAt8, out));

    const WallClockSnapshot tuesdayAt8 = makeWall(20260224, 2, 8, 0, 0, true);
    TEST_ASSERT_TRUE(scheduler.nextDueCommand(0, tuesdayAt8, out));
    TEST_ASSERT_EQUAL(Command::ON, out);
}

void test_scheduler_next_planned_command() {
    CommandScheduler scheduler;
    scheduler.setEnabled(true);
    TEST_ASSERT_TRUE(scheduler.addEntry(7000, Command::OFF));
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(9, 0, 0, Command::ON, kWeekdayAll));

    const WallClockSnapshot wall = makeWall(20260223, 1, 8, 59, 55, true);

    Command next = Command::NONE;
    uint32_t dueInSec = 0;
    bool usesWall = false;
    TEST_ASSERT_TRUE(scheduler.nextPlannedCommand(5000, wall, next, dueInSec, usesWall));
    TEST_ASSERT_EQUAL(Command::OFF, next);
    TEST_ASSERT_FALSE(usesWall);
    TEST_ASSERT_EQUAL_UINT32(2, dueInSec);
}

void test_logger_detail_code_is_recorded() {
    Logger logger;
    WallClockSnapshot ts{};
    ts.valid = true;
    ts.bootMs = 11;
    ts.bootUs = 22;
    ts.unixMs = 1700000000123ULL;
    ts.dateKey = 20260223;
    ts.hour = 7;
    ts.minute = 10;
    ts.second = 11;
    ts.weekday = 1;

    logger.log(ts, LogEventType::TRANSMIT_FAILED, Command::ON, false, 4);

    TEST_ASSERT_EQUAL_UINT32(1, logger.size());
    const LogEntry& first = logger.entries()[0];
    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, first.type);
    TEST_ASSERT_FALSE(first.success);
    TEST_ASSERT_EQUAL_UINT8(4, first.detailCode);
}

void test_ir_sender_reports_hardware_unavailable_in_native() {
    IRSender sender;
    sender.begin();

    const TxFailureCode result = sender.sendCommand(Command::ON);
    TEST_ASSERT_EQUAL(TxFailureCode::HW_UNAVAILABLE, result);
}

void test_hub_mock_scheduler_pushes_expected_commands() {
    HubReceiver hub;
    HubMockScheduler mock;

    // Boot fallback before wall time is valid.
    WallClockSnapshot invalidWall{};
    mock.tick(4000, invalidWall, hub, true);

    Command out = Command::NONE;
    TEST_ASSERT_TRUE(hub.poll(out));
    TEST_ASSERT_EQUAL(Command::ON, out);

    // Wall-clock based schedule.
    const WallClockSnapshot monday7 = makeWall(20260223, 1, 7, 0, 0, true);
    mock.tick(10000, monday7, hub, true);
    TEST_ASSERT_TRUE(hub.poll(out));
    TEST_ASSERT_EQUAL(Command::ON, out);

    // No duplicate trigger same day at same time.
    mock.tick(11000, monday7, hub, true);
    TEST_ASSERT_FALSE(hub.poll(out));

    const WallClockSnapshot monday22 = makeWall(20260223, 1, 22, 0, 0, true);
    mock.tick(12000, monday22, hub, true);
    TEST_ASSERT_TRUE(hub.poll(out));
    TEST_ASSERT_EQUAL(Command::OFF, out);
}

void test_hub_mock_scheduler_can_be_disabled() {
    HubReceiver hub;
    HubMockScheduler mock;

    const WallClockSnapshot wall = makeWall(20260223, 1, 7, 0, 0, true);
    mock.tick(10000, wall, hub, false);

    Command out = Command::NONE;
    TEST_ASSERT_FALSE(hub.poll(out));
}

void test_mock_clock_daily_schedule_at_fixed_times() {
    CommandScheduler scheduler;
    scheduler.setEnabled(true);
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(5, 15, 0, Command::ON, kWeekdayAll));
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(5, 20, 0, Command::OFF, kWeekdayAll));

    MockClock mockClock;
    IClock& clock = mockClock;

    Command out = Command::NONE;

    mockClock.setWallTime(20260223, 1, 5, 14, 59, true);
    TEST_ASSERT_FALSE(scheduler.nextDueCommand(0, clock.now(0, 0), out));

    mockClock.setWallTime(20260223, 1, 5, 15, 0, true);
    TEST_ASSERT_TRUE(scheduler.nextDueCommand(0, clock.now(1000, 2000), out));
    TEST_ASSERT_EQUAL(Command::ON, out);

    out = Command::NONE;
    mockClock.setWallTime(20260223, 1, 5, 15, 30, true);
    TEST_ASSERT_FALSE(scheduler.nextDueCommand(0, clock.now(2000, 3000), out));

    mockClock.setWallTime(20260223, 1, 5, 20, 0, true);
    TEST_ASSERT_TRUE(scheduler.nextDueCommand(0, clock.now(3000, 4000), out));
    TEST_ASSERT_EQUAL(Command::OFF, out);
}

void test_timeline_logs_full_wall_clock_sequence() {
    Logger logger;
    CommandScheduler scheduler;
    scheduler.setEnabled(true);
    const WallClockSnapshot base = hostLocalNow(1000, 1000000);
    if (!base.valid) {
        TEST_IGNORE_MESSAGE("Host local time unavailable");
    }
    if (base.secondsOfDay >= (86400U - 8U)) {
        TEST_IGNORE_MESSAGE("Host local time too close to midnight for this timeline test");
    }

    const uint32_t onSec = base.secondsOfDay + 2U;
    const uint32_t betweenSec = base.secondsOfDay + 3U;
    const uint32_t offSec = base.secondsOfDay + 5U;

    uint8_t onHour = 0;
    uint8_t onMinute = 0;
    uint8_t onSecond = 0;
    uint8_t betweenHour = 0;
    uint8_t betweenMinute = 0;
    uint8_t betweenSecond = 0;
    uint8_t offHour = 0;
    uint8_t offMinute = 0;
    uint8_t offSecond = 0;

    secondsToHms(onSec, onHour, onMinute, onSecond);
    secondsToHms(betweenSec, betweenHour, betweenMinute, betweenSecond);
    secondsToHms(offSec, offHour, offMinute, offSecond);

    TEST_ASSERT_TRUE(scheduler.addDailyEntry(onHour, onMinute, onSecond, Command::ON, kWeekdayAll));
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(offHour, offMinute, offSecond, Command::OFF, kWeekdayAll));

    MockClock clock;

    auto tick = [&](uint32_t nowMs, uint8_t hour, uint8_t minute, uint8_t second) {
        clock.setWallTime(base.dateKey, base.weekday, hour, minute, second, true);
        const WallClockSnapshot wall = clock.now(nowMs, nowMs * 1000U);

        Command due = Command::NONE;
        if (scheduler.nextDueCommand(nowMs, wall, due)) {
            logger.log(wall, LogEventType::SCHEDULE_COMMAND, due, true);
            logger.log(wall, LogEventType::COMMAND_SENT, due, true);
            logger.log(wall, LogEventType::STATE_CHANGE, due, true);
        }
    };

    tick(1000, base.hour, base.minute, base.second);
    tick(2000, onHour, onMinute, onSecond);
    tick(3000, betweenHour, betweenMinute, betweenSecond);
    tick(4000, offHour, offMinute, offSecond);

    TEST_ASSERT_EQUAL_UINT32(6, logger.size());
    // Keep this deterministic test silent; local-time timeline is printed by host test below.
}

void test_host_local_time_timeline_preview() {
    Logger logger;
    const WallClockSnapshot wall = hostLocalNow(5000, 5000000);
    if (!wall.valid) {
        TEST_IGNORE_MESSAGE("Host local time unavailable");
    }

    logger.log(wall, LogEventType::SCHEDULE_COMMAND, Command::ON, true);
    logger.log(wall, LogEventType::COMMAND_SENT, Command::ON, true);
    logger.log(wall, LogEventType::STATE_CHANGE, Command::ON, true);

    TEST_ASSERT_EQUAL_UINT32(3, logger.size());
    printTimeline(logger, "TIMELINE-LOCAL");
}

void test_ntp_clock_from_set_unix_ms_progresses_with_boot_ms() {
    NtpClock clock;
    clock.setUnixTimeMs(1700000000000ULL, 1000U);

    const WallClockSnapshot first = clock.now(1000U, 0U);
    const WallClockSnapshot later = clock.now(2500U, 0U);

    TEST_ASSERT_TRUE(first.valid);
    TEST_ASSERT_TRUE(later.valid);
    TEST_ASSERT_EQUAL_UINT64(1700000000000ULL, first.unixMs);
    TEST_ASSERT_EQUAL_UINT64(1700000001500ULL, later.unixMs);
}

void test_retrofit_logs_command_then_tx_failure_in_native() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(false);

    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::ON));

    WallClockSnapshot wall = hostLocalWithSecondOffset(1000, 1000000, 0);

    controller.tick(1000, 1000000, wall, 20.0F);

    TEST_ASSERT_EQUAL_UINT32(7, logger.size());
    const LogEntry& source = logger.entries()[0];

    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, source.type);
    TEST_ASSERT_EQUAL(Command::ON, source.command);
    TEST_ASSERT_TRUE(source.success);

    uint32_t thermostatControlCount = 0;
    uint32_t txFailCount = 0;
    for (size_t i = 1; i < logger.size(); ++i) {
        const LogEntry& entry = logger.entries()[i];
        if (entry.type == LogEventType::THERMOSTAT_CONTROL) {
            ++thermostatControlCount;
            TEST_ASSERT_EQUAL(Command::TEMP_UP, entry.command);
            TEST_ASSERT_TRUE(entry.success);
        } else if (entry.type == LogEventType::TRANSMIT_FAILED) {
            ++txFailCount;
            TEST_ASSERT_EQUAL(Command::TEMP_UP, entry.command);
            TEST_ASSERT_FALSE(entry.success);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxFailureCode::HW_UNAVAILABLE), entry.detailCode);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(3, thermostatControlCount);
    TEST_ASSERT_EQUAL_UINT32(3, txFailCount);
}

void test_heater_logs_received_command_and_apply_result_without_ack() {
    Heater heater;
    Logger logger;

    WallClockSnapshot wall = hostLocalWithSecondOffset(2000, 2000000, 1);

    const bool applied = heater.applyCommand(Command::ON);
    logger.log(wall, LogEventType::STATE_CHANGE, Command::ON, applied);

    TEST_ASSERT_EQUAL_UINT32(1, logger.size());
    const LogEntry& stateChange = logger.entries()[0];

    TEST_ASSERT_EQUAL(LogEventType::STATE_CHANGE, stateChange.type);
    TEST_ASSERT_EQUAL(Command::ON, stateChange.command);
    TEST_ASSERT_TRUE(stateChange.success);
}

void test_hub_has_priority_over_scheduler_when_both_ready() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(true);

    TEST_ASSERT_TRUE(scheduler.addEntry(500, Command::OFF));
    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::ON));

    WallClockSnapshot wall = hostLocalWithSecondOffset(500, 500000, 3);

    controller.tick(500, 500000, wall, 20.0F);

    TEST_ASSERT_TRUE(logger.size() >= 1);
    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, logger.entries()[0].type);
    TEST_ASSERT_EQUAL(Command::ON, logger.entries()[0].command);

    // Next tick should still execute pending due schedule command.
    wall.bootMs = 520;
    wall.bootUs = 520000;
    controller.tick(520, 520000, wall, 20.0F);

    bool sawScheduleAfterHub = false;
    for (size_t i = 1; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::SCHEDULE_COMMAND &&
            logger.entries()[i].command == Command::OFF) {
            sawScheduleAfterHub = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(sawScheduleAfterHub);
}

void test_hub_temp_up_changes_target_without_transmit_when_power_off() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(false);

    const float initialTarget = controller.healthSnapshot().targetTemperatureC;
    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::TEMP_UP));

    WallClockSnapshot wall = hostLocalWithSecondOffset(600, 600000, 4);

    controller.tick(600, 600000, wall, 20.0F);

    const RetrofitController::HealthSnapshot after = controller.healthSnapshot();
    TEST_ASSERT_FLOAT_WITHIN(0.01F, initialTarget + 1.0F, after.targetTemperatureC);

    TEST_ASSERT_EQUAL_UINT32(1, logger.size());
    const LogEntry& only = logger.entries()[0];
    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, only.type);
    TEST_ASSERT_EQUAL(Command::TEMP_UP, only.command);
}

void test_retrofit_no_retry_drop_logic_on_failed_transmit() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(false);

    controller.powerEnabled_ = true;
    controller.heaterCommandedOn_ = false;
    controller.targetTemperatureC_ = 22.0F;

    WallClockSnapshot first = hostLocalWithSecondOffset(1000, 1000000, 0);
    controller.tick(1000, 1000000, first, 20.0F);
    WallClockSnapshot second = hostLocalWithSecondOffset(1200, 1200000, 1);
    controller.tick(1200, 1200000, second, 20.0F);

    bool sawDrop = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::COMMAND_DROPPED) {
            sawDrop = true;
            break;
        }
    }

    TEST_ASSERT_FALSE(sawDrop);
    TEST_ASSERT_EQUAL_UINT32(6, logger.size());
    for (size_t i = 0; i < logger.size(); i += 2) {
        TEST_ASSERT_EQUAL(LogEventType::THERMOSTAT_CONTROL, logger.entries()[i].type);
        TEST_ASSERT_EQUAL(Command::TEMP_UP, logger.entries()[i].command);
        TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, logger.entries()[i + 1].type);
        TEST_ASSERT_EQUAL(Command::TEMP_UP, logger.entries()[i + 1].command);
    }
}

void test_adaptive_tuning_increases_aggressiveness_when_heating_is_slow() {
    AdaptiveThermostatTuning adaptive;
    const ThermostatTuning base{1.6F, 0.02F, 3.0F, 3};

    adaptive.reset(0U, 20.0F);
    adaptive.onControlStepsSent(0U, 20.0F, 2);

    AdaptiveThermostatTuning::Overrides out =
        adaptive.update(420000U, 20.2F, ThermostatMode::FAST, base);

    TEST_ASSERT_TRUE(out.kp > base.kp);
    TEST_ASSERT_TRUE(out.maxSteps >= base.maxSteps);
}

void test_adaptive_tuning_decreases_aggressiveness_when_heating_is_fast() {
    AdaptiveThermostatTuning adaptive;
    const ThermostatTuning base{1.6F, 0.02F, 3.0F, 3};

    adaptive.reset(0U, 20.0F);
    adaptive.onControlStepsSent(0U, 20.0F, 1);

    AdaptiveThermostatTuning::Overrides out =
        adaptive.update(420000U, 20.8F, ThermostatMode::FAST, base);

    TEST_ASSERT_TRUE(out.kp < base.kp);
    TEST_ASSERT_TRUE(out.maxSteps <= base.maxSteps);
}

void test_adaptive_tuning_does_not_adjust_before_window() {
    AdaptiveThermostatTuning adaptive;
    const ThermostatTuning base{1.6F, 0.02F, 3.0F, 3};

    adaptive.reset(0U, 20.0F);
    adaptive.onControlStepsSent(0U, 20.0F, 1);

    AdaptiveThermostatTuning::Overrides out =
        adaptive.update(60000U, 20.4F, ThermostatMode::FAST, base);

    TEST_ASSERT_FLOAT_WITHIN(0.0001F, base.kp, out.kp);
    TEST_ASSERT_EQUAL_INT(base.maxSteps, out.maxSteps);
}

void test_adaptive_tuning_respects_kp_and_step_bounds() {
    AdaptiveThermostatTuning::Config config;
    config.fastBounds.kpMin = 1.55F;
    config.fastBounds.kpMax = 1.62F;
    config.fastBounds.maxStepsMin = 1;
    config.fastBounds.maxStepsMax = 2;
    AdaptiveThermostatTuning adaptive(config);
    const ThermostatTuning base{1.6F, 0.02F, 3.0F, 3};

    adaptive.reset(0U, 20.0F);
    adaptive.onControlStepsSent(0U, 20.0F, 1);

    AdaptiveThermostatTuning::Overrides out =
        adaptive.update(420000U, 20.1F, ThermostatMode::FAST, base);

    TEST_ASSERT_TRUE(out.kp <= config.fastBounds.kpMax);
    TEST_ASSERT_TRUE(out.kp >= config.fastBounds.kpMin);
    TEST_ASSERT_TRUE(out.maxSteps <= config.fastBounds.maxStepsMax);
    TEST_ASSERT_TRUE(out.maxSteps >= config.fastBounds.maxStepsMin);
}

void test_adaptive_tuning_uses_negative_steps_for_adaptation() {
    AdaptiveThermostatTuning adaptive;
    const ThermostatTuning base{1.6F, 0.02F, 3.0F, 3};

    adaptive.reset(0U, 23.0F);
    adaptive.onControlStepsSent(0U, 23.0F, -1);

    AdaptiveThermostatTuning::Overrides out =
        adaptive.update(420000U, 22.1F, ThermostatMode::FAST, base);

    // Strong cooling response after negative steps means system is very responsive,
    // so adaptation should reduce aggressiveness.
    TEST_ASSERT_TRUE(out.kp < base.kp);
    TEST_ASSERT_TRUE(out.maxSteps <= base.maxSteps);
}

void test_hub_ai_recommendation_is_advisory_only_and_clamped() {
    HubAiInsights insights;

    HubAiInsights::LogEntry sample{};
    sample.timestampMs = 1000;
    sample.roomTemperatureC = 21.0F;
    sample.targetTemperatureC = 22.0F;
    sample.commandSent = HubAiInsights::CommandSent::NONE;
    sample.mode = HubAiInsights::Mode::COMFORT;
    insights.ingest(sample);

    const HubAiInsights::PidRecommendation recommendation = insights.recommendation();
    TEST_ASSERT_TRUE(recommendation.advisoryOnly);
    TEST_ASSERT_TRUE(recommendation.requiresUserIntent);
    TEST_ASSERT_FALSE(recommendation.kdAutoChange);
    TEST_ASSERT_TRUE(recommendation.kpScale >= 0.7F && recommendation.kpScale <= 1.3F);
    TEST_ASSERT_TRUE(recommendation.kiScale >= 0.9F && recommendation.kiScale <= 1.1F);
}

void test_hub_ai_detects_repeated_overshoot() {
    HubAiInsights model;

    HubAiInsights::LogEntry e{};
    e.targetTemperatureC = 22.0F;
    e.commandSent = HubAiInsights::CommandSent::NONE;
    e.mode = HubAiInsights::Mode::COMFORT;

    e.timestampMs = 1000; e.roomTemperatureC = 22.0F; model.ingest(e);
    e.timestampMs = 2000; e.roomTemperatureC = 23.8F; model.ingest(e);
    e.timestampMs = 3000; e.roomTemperatureC = 22.0F; model.ingest(e);
    e.timestampMs = 4000; e.roomTemperatureC = 23.7F; model.ingest(e);

    TEST_ASSERT_TRUE(model.insights().overshoot_detected);
}

void test_hub_ai_detects_window_open_drop() {
    HubAiInsights model;
    HubAiInsights::LogEntry e{};
    e.targetTemperatureC = 22.0F;
    e.mode = HubAiInsights::Mode::COMFORT;
    e.pidOutput = 0.0F;

    e.timestampMs = 0; e.roomTemperatureC = 22.0F; e.commandSent = HubAiInsights::CommandSent::NONE; model.ingest(e);
    e.timestampMs = 60000; e.roomTemperatureC = 21.7F; e.commandSent = HubAiInsights::CommandSent::NONE; model.ingest(e);
    e.timestampMs = 120000; e.roomTemperatureC = 21.4F; e.commandSent = HubAiInsights::CommandSent::NONE; model.ingest(e);

    e.timestampMs = 180000; e.roomTemperatureC = 21.5F; e.targetTemperatureC = 23.0F;
    e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);

    e.timestampMs = 240000; e.roomTemperatureC = 20.0F; e.commandSent = HubAiInsights::CommandSent::NONE; model.ingest(e);

    TEST_ASSERT_TRUE(model.insights().window_open_detected);
}

void test_hub_ai_hardware_failure_detection_and_recovery() {
    HubAiInsights model;
    HubAiInsights::LogEntry e{};
    e.targetTemperatureC = 24.0F;
    e.mode = HubAiInsights::Mode::COMFORT;
    e.roomTemperatureC = 20.0F;

    // Three weak-heating windows with multiple HEAT_UP commands each.
    e.timestampMs = 1000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);
    e.timestampMs = 61000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);
    e.timestampMs = 421000; e.commandSent = HubAiInsights::CommandSent::NONE; e.roomTemperatureC = 20.1F; model.ingest(e);

    e.timestampMs = 422000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; e.roomTemperatureC = 20.1F; model.ingest(e);
    e.timestampMs = 482000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);
    e.timestampMs = 842000; e.commandSent = HubAiInsights::CommandSent::NONE; e.roomTemperatureC = 20.2F; model.ingest(e);

    e.timestampMs = 843000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; e.roomTemperatureC = 20.2F; model.ingest(e);
    e.timestampMs = 903000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);
    e.timestampMs = 1263000; e.commandSent = HubAiInsights::CommandSent::NONE; e.roomTemperatureC = 20.3F; model.ingest(e);

    TEST_ASSERT_TRUE(model.insights().hardware_failure_detected);
    TEST_ASSERT_TRUE(model.probableHardwareCause()[0] != '\0');

    // Recovery window: normal heating resumes.
    e.timestampMs = 1264000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; e.roomTemperatureC = 20.3F; model.ingest(e);
    e.timestampMs = 1324000; e.commandSent = HubAiInsights::CommandSent::HEAT_UP; model.ingest(e);
    e.timestampMs = 1684000; e.commandSent = HubAiInsights::CommandSent::NONE; e.roomTemperatureC = 21.0F; model.ingest(e);

    TEST_ASSERT_FALSE(model.insights().hardware_failure_detected);
}

void test_hub_ai_eco_kp_is_lower_than_comfort() {
    HubAiInsights model;

    HubAiInsights::LogEntry e{};
    e.timestampMs = 1000;
    e.roomTemperatureC = 21.0F;
    e.targetTemperatureC = 22.0F;
    e.commandSent = HubAiInsights::CommandSent::NONE;
    e.mode = HubAiInsights::Mode::COMFORT;
    model.ingest(e);
    const float comfortKp = model.insights().kp_scale;

    e.timestampMs = 2000;
    e.mode = HubAiInsights::Mode::ECO;
    e.roomTemperatureC = 21.2F;
    model.ingest(e);
    const float ecoKp = model.insights().kp_scale;

    TEST_ASSERT_TRUE(ecoKp < comfortKp);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_scheduler_relative_due);
    RUN_TEST(test_scheduler_daily_once_per_day);
    RUN_TEST(test_scheduler_next_planned_command);
    RUN_TEST(test_logger_detail_code_is_recorded);
    RUN_TEST(test_ir_sender_reports_hardware_unavailable_in_native);
    RUN_TEST(test_hub_mock_scheduler_pushes_expected_commands);
    RUN_TEST(test_hub_mock_scheduler_can_be_disabled);
    RUN_TEST(test_mock_clock_daily_schedule_at_fixed_times);
    RUN_TEST(test_timeline_logs_full_wall_clock_sequence);
    RUN_TEST(test_host_local_time_timeline_preview);
    RUN_TEST(test_ntp_clock_from_set_unix_ms_progresses_with_boot_ms);
    RUN_TEST(test_retrofit_logs_command_then_tx_failure_in_native);
    RUN_TEST(test_heater_logs_received_command_and_apply_result_without_ack);
    RUN_TEST(test_hub_has_priority_over_scheduler_when_both_ready);
    RUN_TEST(test_hub_temp_up_changes_target_without_transmit_when_power_off);
    RUN_TEST(test_retrofit_no_retry_drop_logic_on_failed_transmit);
    RUN_TEST(test_adaptive_tuning_increases_aggressiveness_when_heating_is_slow);
    RUN_TEST(test_adaptive_tuning_decreases_aggressiveness_when_heating_is_fast);
    RUN_TEST(test_adaptive_tuning_does_not_adjust_before_window);
    RUN_TEST(test_adaptive_tuning_respects_kp_and_step_bounds);
    RUN_TEST(test_adaptive_tuning_uses_negative_steps_for_adaptation);
    RUN_TEST(test_hub_ai_recommendation_is_advisory_only_and_clamped);
    RUN_TEST(test_hub_ai_detects_repeated_overshoot);
    RUN_TEST(test_hub_ai_detects_window_open_drop);
    RUN_TEST(test_hub_ai_hardware_failure_detection_and_recovery);
    RUN_TEST(test_hub_ai_eco_kp_is_lower_than_comfort);

    return UNITY_END();
}
