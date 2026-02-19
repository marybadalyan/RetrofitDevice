#include "IRReciever.h"
#include "IRSender.h"
#include "app/retrofit_controller.h"
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
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger);

void loadDefaultSchedule() {
    // Relative one-shot entries (legacy-compatible).
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);

    // Daily wall-clock entries (local time).
    gScheduler.addDailyEntry(8, 0, 0, Command::ON, kWeekdayWeekdays);
    gScheduler.addDailyEntry(22, 0, 0, Command::OFF, kWeekdayAll);
}

void mockHubInput(uint32_t nowMs) {
    // Mock hub commands for fallback mode; replace with WiFi/Blynk later.
    static bool pushed = false;
    if (kSchedulerEnabled) {
        return;
    }
    if (!pushed && nowMs > 3000) {
        gHubReceiver.pushMockCommand(Command::ON);
        gHubReceiver.pushMockCommand(Command::TEMP_UP);
        pushed = true;
    }
}
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif

    gIrSender.begin();
    gIrReceiver.begin();

    gLogger.beginPersistence("retrofit-log");
    gWallClock.beginNtp(kNtpTimezone, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);

    gController.begin(kSchedulerEnabled);

    if (kSchedulerEnabled) {
        loadDefaultSchedule();
    }
}

void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

    mockHubInput(nowMs);

    // Scheduler/hub arbitration + ACK handling live in controller.
    gController.tick(nowMs, nowUs, wallNow);
}
