#include <unity.h>
#include <cstdio>
#include <ctime>

#include "IRSender.h"
#define private public
#include "IRReciever.h"
#include "app/retrofit_controller.h"
#undef private
#include "heater/heater.h"
#include "hub/hub_mock_scheduler.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "protocol.h"
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
        case LogEventType::ACK_RECEIVED:
            return "ACK_RECEIVED";
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
        case LogEventType::ACK_SENT:
            return "ACK_SENT";
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

void injectAckFrame(IRReceiver& receiver, Command command) {
    const protocol::Packet ackPacket = protocol::makeAck(command);

    size_t index = 0;
    receiver.pulseDurationsUs_[index++] = 9000;
    receiver.pulseDurationsUs_[index++] = 4500;

    auto appendByte = [&](uint8_t value) {
        for (int bit = 7; bit >= 0; --bit) {
            receiver.pulseDurationsUs_[index++] = 560;
            const bool one = (value & (1U << bit)) != 0U;
            receiver.pulseDurationsUs_[index++] = one ? 1690 : 560;
        }
    };

    appendByte(static_cast<uint8_t>(ackPacket.header >> 8));
    appendByte(static_cast<uint8_t>(ackPacket.header & 0xFFU));
    appendByte(ackPacket.command);
    appendByte(ackPacket.checksum);
    receiver.pulseCount_ = index;
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
            logger.log(wall, LogEventType::ACK_RECEIVED, due, true);
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
    logger.log(wall, LogEventType::ACK_RECEIVED, Command::ON, true);

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

    TEST_ASSERT_EQUAL_UINT32(3, logger.size());
    const LogEntry& source = logger.entries()[0];
    const LogEntry& thermostat = logger.entries()[1];
    const LogEntry& txFail = logger.entries()[2];

    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, source.type);
    TEST_ASSERT_EQUAL(Command::ON, source.command);
    TEST_ASSERT_TRUE(source.success);

    TEST_ASSERT_EQUAL(LogEventType::THERMOSTAT_CONTROL, thermostat.type);
    TEST_ASSERT_EQUAL(Command::ON, thermostat.command);
    TEST_ASSERT_TRUE(thermostat.success);

    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, txFail.type);
    TEST_ASSERT_EQUAL(Command::ON, txFail.command);
    TEST_ASSERT_FALSE(txFail.success);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TxFailureCode::HW_UNAVAILABLE), txFail.detailCode);
}

void test_heater_logs_received_command_and_apply_result() {
    Heater heater;
    IRSender sender;
    sender.begin();
    Logger logger;

    WallClockSnapshot wall = hostLocalWithSecondOffset(2000, 2000000, 1);

    const bool applied = heater.applyCommand(Command::ON);
    logger.log(wall, LogEventType::STATE_CHANGE, Command::ON, applied);

    if (applied) {
        const TxFailureCode ackResult = sender.sendAck(Command::ON);
        if (ackResult != TxFailureCode::NONE) {
            logger.log(wall,
                       LogEventType::TRANSMIT_FAILED,
                       Command::ON,
                       false,
                       static_cast<uint8_t>(ackResult));
        }
    }

    TEST_ASSERT_EQUAL_UINT32(2, logger.size());
    const LogEntry& stateChange = logger.entries()[0];
    const LogEntry& txFail = logger.entries()[1];

    TEST_ASSERT_EQUAL(LogEventType::STATE_CHANGE, stateChange.type);
    TEST_ASSERT_EQUAL(Command::ON, stateChange.command);
    TEST_ASSERT_TRUE(stateChange.success);

    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, txFail.type);
    TEST_ASSERT_EQUAL(Command::ON, txFail.command);
    TEST_ASSERT_FALSE(txFail.success);
}

void test_retrofit_logs_ack_received_for_pending_command() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(false);

    controller.pendingStatus_ = RetrofitController::PendingStatus::WAITING_ACK;
    controller.pendingCommand_ = Command::ON;
    controller.retryCount_ = 1;
    controller.powerEnabled_ = true;

    injectAckFrame(receiver, Command::ON);

    WallClockSnapshot wall = hostLocalWithSecondOffset(3000, 3000000, 2);

    controller.tick(3000, 3000000, wall, 20.0F);

    TEST_ASSERT_EQUAL_UINT32(1, logger.size());
    const LogEntry& ack = logger.entries()[0];
    TEST_ASSERT_EQUAL(LogEventType::ACK_RECEIVED, ack.type);
    TEST_ASSERT_EQUAL(Command::ON, ack.command);
    TEST_ASSERT_TRUE(ack.success);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::IDLE, controller.pendingStatus_);
    TEST_ASSERT_EQUAL(Command::NONE, controller.pendingCommand_);
    TEST_ASSERT_EQUAL_UINT8(0, controller.retryCount_);
    TEST_ASSERT_TRUE(controller.heaterCommandedOn_);
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
    TEST_ASSERT_FALSE(after.waitingAck);

    TEST_ASSERT_EQUAL_UINT32(1, logger.size());
    const LogEntry& only = logger.entries()[0];
    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, only.type);
    TEST_ASSERT_EQUAL(Command::TEMP_UP, only.command);
}

void test_retrofit_timeout_retries_then_drops_pending_command() {
    IRSender sender;
    sender.begin();
    IRReceiver receiver;
    receiver.begin();
    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController controller(sender, receiver, hub, scheduler, logger);
    controller.begin(false);

    controller.pendingStatus_ = RetrofitController::PendingStatus::WAITING_ACK;
    controller.pendingCommand_ = Command::ON;
    controller.pendingDeadlineMs_ = 1000;
    controller.retryCount_ = 0;
    controller.powerEnabled_ = true;

    const WallClockSnapshot baseWall = hostLocalWithSecondOffset(1000, 1000000, 0);
    auto tickTimeout = [&](uint32_t nowMs) {
        const uint32_t offsetSec = nowMs / 1000U;
        uint8_t hour = baseWall.hour;
        uint8_t minute = baseWall.minute;
        uint8_t second = baseWall.second;
        secondsToHms(baseWall.secondsOfDay + offsetSec, hour, minute, second);

        WallClockSnapshot wall = baseWall;
        wall.bootMs = nowMs;
        wall.bootUs = nowMs * 1000U;
        wall.hour = hour;
        wall.minute = minute;
        wall.second = second;
        wall.secondsOfDay =
            (static_cast<uint32_t>(wall.hour) * 3600UL) + (static_cast<uint32_t>(wall.minute) * 60UL) + wall.second;
        controller.tick(nowMs, wall.bootUs, wall, 20.0F);
    };

    tickTimeout(1000);
    tickTimeout(1200);
    tickTimeout(1400);

    const RetrofitController::HealthSnapshot after = controller.healthSnapshot();
    TEST_ASSERT_FALSE(after.waitingAck);
    TEST_ASSERT_EQUAL(Command::NONE, after.pendingCommand);
    TEST_ASSERT_EQUAL_UINT8(0, after.retryCount);

    TEST_ASSERT_EQUAL_UINT32(3, logger.size());
    const LogEntry& firstRetry = logger.entries()[0];
    const LogEntry& secondRetry = logger.entries()[1];
    const LogEntry& drop = logger.entries()[2];
    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, firstRetry.type);
    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, secondRetry.type);
    TEST_ASSERT_EQUAL(LogEventType::COMMAND_DROPPED, drop.type);

    // Cooldown prevents an immediate thermostat resend after drop.
    tickTimeout(2000);
    TEST_ASSERT_EQUAL_UINT32(3, logger.size());

    // After cooldown expiry, thermostat can re-attempt.
    tickTimeout(2500);
    TEST_ASSERT_EQUAL_UINT32(5, logger.size());
    TEST_ASSERT_EQUAL(LogEventType::THERMOSTAT_CONTROL, logger.entries()[3].type);
    TEST_ASSERT_EQUAL(LogEventType::TRANSMIT_FAILED, logger.entries()[4].type);
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
    RUN_TEST(test_heater_logs_received_command_and_apply_result);
    RUN_TEST(test_retrofit_logs_ack_received_for_pending_command);
    RUN_TEST(test_hub_has_priority_over_scheduler_when_both_ready);
    RUN_TEST(test_hub_temp_up_changes_target_without_transmit_when_power_off);
    RUN_TEST(test_retrofit_timeout_retries_then_drops_pending_command);

    return UNITY_END();
}
