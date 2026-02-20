#include "IRReciever.h"
#include "IRSender.h"
#include "app/room_temp_sensor.h"
#include "app/retrofit_controller.h"
#include "hub/hub_connectivity.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "prefferences.h"
#include "scheduler/scheduler.h"
#include "time/wall_clock.h"

#include <array>

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
static inline uint32_t micros() { return 0; }
#endif

namespace {
IRSender gIrSender;
IRReceiver gIrReceiver;
HubReceiver gHubReceiver;
CommandScheduler gScheduler;
Logger gLogger;
WallClock gWallClock;
RoomTempSensor gRoomTempSensor;
HubConnectivity gHubConnectivity;
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger);

void loadDefaultSchedule() {
    // Relative one-shot entries (legacy-compatible).
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);

    // Daily wall-clock entries (local time).
    gScheduler.addDailyEntry(8, 0, 0, Command::ON, kWeekdayWeekdays);
    gScheduler.addDailyEntry(22, 0, 0, Command::OFF, kWeekdayAll);
}

struct HubMockScheduleEntry {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekdayMask;
    Command command;
    uint32_t lastFiredDateKey;
};

void mockHubInput(uint32_t nowMs, const WallClockSnapshot& wallNow) {
    // Mock hub commands for fallback mode; replace with real hub transport later.
    if (kSchedulerEnabled) {
        return;
    }

    static std::array<HubMockScheduleEntry, 2> schedule = {{
        {7, 0, 0, kWeekdayAll, Command::ON, 0},
        {22, 0, 0, kWeekdayAll, Command::OFF, 0},
    }};

    if (!wallNow.valid || wallNow.dateKey == 0U) {
        // Before time sync, keep minimal relative fallback behavior.
        static bool bootstrapPushed = false;
        if (!bootstrapPushed && nowMs > 3000) {
            gHubReceiver.pushMockCommand(Command::ON);
            bootstrapPushed = true;
        }
        return;
    }

    const uint8_t todayBit = static_cast<uint8_t>(1U << wallNow.weekday);
    for (HubMockScheduleEntry& entry : schedule) {
        if ((entry.weekdayMask & todayBit) == 0U) {
            continue;
        }
        if (entry.lastFiredDateKey == wallNow.dateKey) {
            continue;
        }

        const uint32_t targetSeconds =
            (static_cast<uint32_t>(entry.hour) * 3600UL) + (static_cast<uint32_t>(entry.minute) * 60UL) + entry.second;
        if (wallNow.secondsOfDay < targetSeconds) {
            continue;
        }

        if (gHubReceiver.pushMockCommand(entry.command)) {
            entry.lastFiredDateKey = wallNow.dateKey;
        }
    }
}
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif

    gIrSender.begin();
    gIrReceiver.begin();
    gRoomTempSensor.begin();

    gLogger.beginPersistence("retrofit-log");
    gHubConnectivity.begin(gHubReceiver, gWallClock);

    gController.begin(kSchedulerEnabled);

    if (kSchedulerEnabled) {
        loadDefaultSchedule();
    }
}

void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    gHubConnectivity.tick(nowMs, gHubReceiver, gWallClock);
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);
    const float roomTemperatureC = gRoomTempSensor.readTemperatureC();

    if (!gHubConnectivity.wifiConnected()) {
        mockHubInput(nowMs, wallNow);
    }

    // Scheduler/hub arbitration + ACK handling live in controller.
    gController.tick(nowMs, nowUs, wallNow, roomTemperatureC);
}
