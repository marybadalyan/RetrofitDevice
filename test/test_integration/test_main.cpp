#include <unity.h>

#define private public
#include "IRSender.h"
#include "IRReciever.h"
#include "app/retrofit_controller.h"
#undef private

#include "hub/hub_receiver.h"
#include "logger.h"
#include "scheduler/scheduler.h"

namespace {

WallClockSnapshot makeWall(uint32_t dateKey,
                           uint8_t weekday,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second,
                           uint32_t bootMs,
                           uint32_t bootUs) {
    WallClockSnapshot snapshot{};
    snapshot.valid = true;
    snapshot.dateKey = dateKey;
    snapshot.weekday = weekday;
    snapshot.hour = hour;
    snapshot.minute = minute;
    snapshot.second = second;
    snapshot.secondsOfDay =
        (static_cast<uint32_t>(hour) * 3600UL) + (static_cast<uint32_t>(minute) * 60UL) + static_cast<uint32_t>(second);
    snapshot.bootMs = bootMs;
    snapshot.bootUs = bootUs;
    return snapshot;
}

void forceSenderReady(IRSender& sender) {
    sender.hardwareAvailable_ = true;
    sender.initialized_ = true;
}

void test_integration_hub_on_updates_state_without_ack() {
    IRSender sender;
    sender.begin();
    forceSenderReady(sender);

    IRReceiver receiver;
    receiver.begin();

    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController retrofit(sender, receiver, hub, scheduler, logger);
    retrofit.begin(false);

    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::ON));

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 0, 0, 1000, 1000000);
    retrofit.tick(1000, 1000000, wall, 20.0F);

    TEST_ASSERT_TRUE(retrofit.powerEnabled_);
    bool sawTempUp = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::THERMOSTAT_CONTROL &&
            logger.entries()[i].command == Command::TEMP_UP) {
            sawTempUp = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(sawTempUp);
}

void test_integration_thermostat_turns_off_and_heater_applies_off() {
    IRSender sender;
    sender.begin();
    forceSenderReady(sender);

    IRReceiver receiver;
    receiver.begin();

    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController retrofit(sender, receiver, hub, scheduler, logger);
    retrofit.begin(false);

    retrofit.powerEnabled_ = true;
    retrofit.targetTemperatureC_ = 22.0F;

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 10, 0, 2000, 2000000);
    retrofit.tick(2000, 2000000, wall, 24.0F);

    bool sawTempDown = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::THERMOSTAT_CONTROL &&
            logger.entries()[i].command == Command::TEMP_DOWN) {
            sawTempDown = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(sawTempDown);
}

void test_integration_no_command_drop_without_ack_logic() {
    IRSender sender;
    sender.begin();
    forceSenderReady(sender);

    IRReceiver receiver;
    receiver.begin();

    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController retrofit(sender, receiver, hub, scheduler, logger);
    retrofit.begin(false);

    retrofit.powerEnabled_ = true;
    retrofit.heaterCommandedOn_ = false;
    retrofit.targetTemperatureC_ = 22.0F;

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 20, 0, 1000, 1000000);
    retrofit.tick(1000, 1000000, wall, 30.0F);

    bool sawDrop = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::COMMAND_DROPPED) {
            sawDrop = true;
        }
    }
    TEST_ASSERT_FALSE(sawDrop);
}

void test_integration_hub_overrides_scheduler_for_current_tick() {
    IRSender sender;
    sender.begin();
    forceSenderReady(sender);

    IRReceiver receiver;
    receiver.begin();

    HubReceiver hub;
    CommandScheduler scheduler;
    Logger logger;
    RetrofitController retrofit(sender, receiver, hub, scheduler, logger);
    retrofit.begin(true);

    TEST_ASSERT_TRUE(scheduler.addEntry(500, Command::OFF));
    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::ON));

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 30, 0, 500, 500000);
    retrofit.tick(500, 500000, wall, 20.0F);

    TEST_ASSERT_TRUE(logger.size() >= 1);
    TEST_ASSERT_EQUAL(LogEventType::HUB_COMMAND_RX, logger.entries()[0].type);
    TEST_ASSERT_EQUAL(Command::ON, logger.entries()[0].command);

    wall.bootMs = 520;
    wall.bootUs = 520000;
    retrofit.tick(520, 520000, wall, 20.0F);

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

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_integration_hub_on_updates_state_without_ack);
    RUN_TEST(test_integration_thermostat_turns_off_and_heater_applies_off);
    RUN_TEST(test_integration_no_command_drop_without_ack_logic);
    RUN_TEST(test_integration_hub_overrides_scheduler_for_current_tick);

    return UNITY_END();
}
