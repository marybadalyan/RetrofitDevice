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
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(5, 15, 0, Command::ON, kWeekdayAll));
    TEST_ASSERT_TRUE(scheduler.addDailyEntry(5, 20, 0, Command::OFF, kWeekdayAll));

    MockClock clock;

    auto tick = [&](uint32_t nowMs, uint8_t hour, uint8_t minute, uint8_t second) {
        clock.setWallTime(20260223, 1, hour, minute, second, true);
        const WallClockSnapshot wall = clock.now(nowMs, nowMs * 1000U);

        Command due = Command::NONE;
        if (scheduler.nextDueCommand(nowMs, wall, due)) {
            logger.log(wall, LogEventType::SCHEDULE_COMMAND, due, true);
            logger.log(wall, LogEventType::COMMAND_SENT, due, true);
            logger.log(wall, LogEventType::ACK_RECEIVED, due, true);
        }
    };

    tick(1000, 5, 14, 59);
    tick(2000, 5, 15, 0);
    tick(3000, 5, 16, 0);
    tick(4000, 5, 20, 0);

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

    WallClockSnapshot wall = makeWall(20260223, 2, 7, 0, 0, true);
    wall.bootMs = 1000;
    wall.bootUs = 1000000;

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

    WallClockSnapshot wall = makeWall(20260223, 2, 7, 0, 1, true);
    wall.bootMs = 2000;
    wall.bootUs = 2000000;

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

    WallClockSnapshot wall = makeWall(20260223, 2, 7, 0, 2, true);
    wall.bootMs = 3000;
    wall.bootUs = 3000000;

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

    return UNITY_END();
}
