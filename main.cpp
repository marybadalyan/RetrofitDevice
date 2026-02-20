#include "IRReciever.h"
#include "IRSender.h"
#include "app/room_temp_sensor.h"
#include "app/retrofit_controller.h"
#include "diagnostics/diag.h"
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

void printHealthSnapshot(uint32_t nowMs, const WallClockSnapshot& wallNow, float roomTemperatureC) {
    static uint32_t lastPrintMs = 0;
    if (!diag::enabled(DiagLevel::INFO)) {
        return;
    }
    if (nowMs - lastPrintMs < kHealthSnapshotIntervalMs) {
        return;
    }
    lastPrintMs = nowMs;

#if __has_include(<Arduino.h>)
    const RetrofitController::HealthSnapshot health = gController.healthSnapshot();

    Serial.println("[INFO] [HEALTH] ----------------");
    Serial.print("wifi_connected=");
    Serial.println(gHubConnectivity.wifiConnected() ? "true" : "false");
    Serial.print("time_valid=");
    Serial.println(wallNow.valid ? "true" : "false");
    if (wallNow.valid) {
        Serial.print("local_time=");
        Serial.print(wallNow.year);
        Serial.print("-");
        Serial.print(wallNow.month);
        Serial.print("-");
        Serial.print(wallNow.day);
        Serial.print(" ");
        Serial.print(wallNow.hour);
        Serial.print(":");
        Serial.print(wallNow.minute);
        Serial.print(":");
        Serial.println(wallNow.second);
    }

    Serial.print("scheduler_enabled=");
    Serial.println(gScheduler.enabled() ? "true" : "false");
    Command nextPlanned = Command::NONE;
    uint32_t nextDueSec = 0;
    bool nextUsesWall = false;
    if (gScheduler.nextPlannedCommand(nowMs, wallNow, nextPlanned, nextDueSec, nextUsesWall)) {
        Serial.print("scheduler_next_cmd=");
        Serial.print(commandToString(nextPlanned));
        Serial.print(" due_in_sec=");
        Serial.print(nextDueSec);
        Serial.print(" mode=");
        Serial.println(nextUsesWall ? "wall" : "relative");
    } else {
        Serial.println("scheduler_next_cmd=none");
    }

    Serial.print("thermostat_target_c=");
    Serial.println(health.targetTemperatureC, 1);
    Serial.print("room_temp_c=");
    Serial.println(roomTemperatureC, 1);
    Serial.print("thermostat_power_enabled=");
    Serial.println(health.powerEnabled ? "true" : "false");
    Serial.print("heater_commanded_on=");
    Serial.println(health.heaterCommandedOn ? "true" : "false");

    Serial.print("pending_ack=");
    Serial.println(health.waitingAck ? "true" : "false");
    Serial.print("retry_count=");
    Serial.println(health.retryCount);
    Serial.print("pending_cmd=");
    Serial.println(commandToString(health.pendingCommand));
    Serial.print("last_tx_failure_code=");
    Serial.println(static_cast<uint8_t>(health.lastTxFailure));
#else
    (void)wallNow;
    (void)roomTemperatureC;
#endif
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
    printHealthSnapshot(nowMs, wallNow, roomTemperatureC);
}
