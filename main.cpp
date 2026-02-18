#include "IRReciever.h"
#include "IRSender.h"
#include "app/retrofit_controller.h"
#include "hub/blynk_bridge.h"
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
BlynkBridge gBlynkBridge;
CommandScheduler gScheduler;
Logger gLogger;
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger);

void loadDefaultSchedule() {
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);
    gScheduler.addEntry(12000, Command::OFF);
}

void mockHubInput(uint32_t nowMs) {
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

void mockBlynkInput(uint32_t nowMs) {
    if (!kEnableMockBlynk) {
        return;
    }

    static bool commandPushed = false;
    if (!commandPushed && nowMs > 3000) {
        gBlynkBridge.pushControlCommand(Command::ON);
        commandPushed = true;
    }
}

void processBlynkControl(uint32_t nowMs) {
    Command control = Command::NONE;
    while (gBlynkBridge.pollControlCommand(control)) {
        gLogger.log(nowMs, LogEventType::BLYNK_COMMAND_RX, control, true);
        (void)gController.sendImmediate(control, nowMs, LogEventType::BLYNK_COMMAND_RX);
    }
}
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif

    gIrSender.begin();
    gIrReceiver.begin();
    gBlynkBridge.begin();
    gController.begin(kSchedulerEnabled);

    if (kSchedulerEnabled) {
        loadDefaultSchedule();
    }
}

void loop() {
    const uint32_t nowMs = millis();

    mockHubInput(nowMs);
    mockBlynkInput(nowMs);
    processBlynkControl(nowMs);
    gController.tick(nowMs);
}
