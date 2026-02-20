#include <unity.h>

#include "IRSender.h"
#include "hub/hub_mock_scheduler.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "scheduler/scheduler.h"

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

    return UNITY_END();
}
