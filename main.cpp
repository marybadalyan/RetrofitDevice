#include "IRReciever.h"
#include "IRSender.h"
#include "app/retrofit_controller.h"
#include "hub/hub_receiver.h"
#include "logger.h"
#include "prefferences.h"
#include "scheduler/scheduler.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
#endif

namespace {
IRSender gIrSender;
IRReceiver gIrReceiver;
HubReceiver gHubReceiver;
CommandScheduler gScheduler;
Logger gLogger;
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger);

void loadDefaultSchedule() {
    // Simple schedule example: execute relative to boot time.
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);
    gScheduler.addEntry(12000, Command::OFF);
}

void mockHubInput(uint32_t nowMs) {
    // Mock hub commands for fallback mode; replace with WiFi/MQTT later.
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
    gController.begin(kSchedulerEnabled);

    if (kSchedulerEnabled) {
        loadDefaultSchedule();
    }
}

void loop() {
    const uint32_t nowMs = millis();
    mockHubInput(nowMs);

    // Scheduler/hub arbitration + ACK handling live in controller.
    gController.tick(nowMs);
}
