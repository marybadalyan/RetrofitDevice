#include <unity.h>

#define private public
#include "IRSender.h"
#include "IRReciever.h"
#include "app/retrofit_controller.h"
#undef private

#include "heater/heater.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "protocol.h"
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

void test_integration_hub_on_roundtrip_ack_updates_state() {
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

    Heater heater;

    TEST_ASSERT_TRUE(hub.pushMockCommand(Command::ON));

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 0, 0, 1000, 1000000);
    retrofit.tick(1000, 1000000, wall, 20.0F);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::WAITING_ACK, retrofit.pendingStatus_);
    TEST_ASSERT_EQUAL(Command::ON, retrofit.pendingCommand_);

    TEST_ASSERT_TRUE(heater.applyCommand(retrofit.pendingCommand_));
    TEST_ASSERT_TRUE(heater.powerEnabled());

    injectAckFrame(receiver, Command::ON);
    wall.bootMs = 1020;
    wall.bootUs = 1020000;
    retrofit.tick(1020, 1020000, wall, 20.0F);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::IDLE, retrofit.pendingStatus_);
    TEST_ASSERT_EQUAL(Command::NONE, retrofit.pendingCommand_);
    TEST_ASSERT_TRUE(retrofit.heaterCommandedOn_);

    bool sawAck = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::ACK_RECEIVED && logger.entries()[i].command == Command::ON) {
            sawAck = true;
        }
    }
    TEST_ASSERT_TRUE(sawAck);
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

    Heater heater;
    TEST_ASSERT_TRUE(heater.applyCommand(Command::ON));

    retrofit.powerEnabled_ = true;
    retrofit.heaterCommandedOn_ = true;
    retrofit.targetTemperatureC_ = 22.0F;

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 10, 0, 2000, 2000000);
    retrofit.tick(2000, 2000000, wall, 24.0F);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::WAITING_ACK, retrofit.pendingStatus_);
    TEST_ASSERT_EQUAL(Command::OFF, retrofit.pendingCommand_);

    TEST_ASSERT_TRUE(heater.applyCommand(Command::OFF));
    TEST_ASSERT_FALSE(heater.powerEnabled());

    injectAckFrame(receiver, Command::OFF);
    wall.bootMs = 2020;
    wall.bootUs = 2020000;
    retrofit.tick(2020, 2020000, wall, 24.0F);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::IDLE, retrofit.pendingStatus_);
    TEST_ASSERT_FALSE(retrofit.heaterCommandedOn_);
}

void test_integration_retries_then_drops_when_no_ack_received() {
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

    retrofit.pendingStatus_ = RetrofitController::PendingStatus::WAITING_ACK;
    retrofit.pendingCommand_ = Command::ON;
    retrofit.pendingDeadlineMs_ = 1000;
    retrofit.retryCount_ = 0;

    WallClockSnapshot wall = makeWall(20260223, 1, 6, 20, 0, 1000, 1000000);
    retrofit.tick(1000, 1000000, wall, 20.0F);

    wall.bootMs = 1120;
    wall.bootUs = 1120000;
    retrofit.tick(1120, 1120000, wall, 20.0F);

    wall.bootMs = 1240;
    wall.bootUs = 1240000;
    retrofit.tick(1240, 1240000, wall, 20.0F);

    TEST_ASSERT_EQUAL(RetrofitController::PendingStatus::IDLE, retrofit.pendingStatus_);
    TEST_ASSERT_EQUAL(Command::NONE, retrofit.pendingCommand_);
    TEST_ASSERT_EQUAL_UINT8(0, retrofit.retryCount_);

    bool sawDrop = false;
    for (size_t i = 0; i < logger.size(); ++i) {
        if (logger.entries()[i].type == LogEventType::COMMAND_DROPPED && logger.entries()[i].command == Command::ON) {
            sawDrop = true;
        }
    }
    TEST_ASSERT_TRUE(sawDrop);
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

    RUN_TEST(test_integration_hub_on_roundtrip_ack_updates_state);
    RUN_TEST(test_integration_thermostat_turns_off_and_heater_applies_off);
    RUN_TEST(test_integration_retries_then_drops_when_no_ack_received);
    RUN_TEST(test_integration_hub_overrides_scheduler_for_current_tick);

    return UNITY_END();
}
