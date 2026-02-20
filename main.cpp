#include "IRReciever.h"
#include "IRSender.h"
#include "app/room_temp_sensor.h"
#include "app/retrofit_controller.h"
#include "diagnostics/diag.h"
#include "hub/hub_connectivity.h"
#include "hub/hub_mock_scheduler.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "prefferences.h"
#include "scheduler/scheduler.h"
#include "time/wall_clock.h"

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
HubMockScheduler gHubMockScheduler;
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger);

void loadDefaultSchedule() {
    // Relative one-shot entries (legacy-compatible).
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);

    // Daily wall-clock entries (local time).
    gScheduler.addDailyEntry(8, 0, 0, Command::ON, kWeekdayWeekdays);
    gScheduler.addDailyEntry(22, 0, 0, Command::OFF, kWeekdayAll);
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

    gHubMockScheduler.tick(nowMs,
                           wallNow,
                           gHubReceiver,
                           (!gHubConnectivity.wifiConnected()) && kEnableHubMockScheduler);

    // Scheduler/hub arbitration + ACK handling live in controller.
    gController.tick(nowMs, nowUs, wallNow, roomTemperatureC);
    printHealthSnapshot(nowMs, wallNow, roomTemperatureC);
}
