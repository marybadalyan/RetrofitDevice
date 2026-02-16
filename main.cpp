#include "IRReciever.h"
#include "IRSender.h"
#include "app/retrofit_controller.h"
#include "hub/blynk_bridge.h"
#include "hub/hub_receiver.h"
#include "learning/learned_command_store.h"
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
LearnedCommandStore gLearnedStore;
RetrofitController gController(gIrSender, gIrReceiver, gHubReceiver, gScheduler, gLogger, gLearnedStore);

bool gLearningActive = false;
Command gLearningTarget = Command::NONE;
uint32_t gLearningDeadlineMs = 0;

void loadDefaultSchedule() {
    gScheduler.addEntry(2000, Command::ON);
    gScheduler.addEntry(6000, Command::TEMP_UP);
    gScheduler.addEntry(12000, Command::OFF);
}

void startLearning(Command target, uint32_t nowMs) {
    gLearningActive = true;
    gLearningTarget = target;
    gLearningDeadlineMs = nowMs + kLearningTimeoutMs;
    gLogger.log(nowMs, LogEventType::LEARNING_START, target, true);
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

    static bool learnPushed = false;
    static bool commandPushed = false;

    if (!learnPushed && nowMs > 2000) {
        gBlynkBridge.pushLearnRequest(Command::ON);
        learnPushed = true;
    }

    if (!commandPushed && nowMs > 8000) {
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

void processLearning(uint32_t nowMs) {
    Command learnTarget = Command::NONE;
    while (gBlynkBridge.pollLearnRequest(learnTarget)) {
        startLearning(learnTarget, nowMs);
    }

    if (!gLearningActive) {
        return;
    }

    RawIRFrame frame{};
    if (gIrReceiver.pollRawFrame(frame)) {
        const bool saved = gLearnedStore.save(gLearningTarget, frame);
        gLogger.log(nowMs, LogEventType::LEARNING_SUCCESS, gLearningTarget, saved);
        gLearningActive = false;
        gLearningTarget = Command::NONE;
        return;
    }

    if (static_cast<int32_t>(nowMs - gLearningDeadlineMs) >= 0) {
        gLogger.log(nowMs, LogEventType::LEARNING_TIMEOUT, gLearningTarget, false);
        gLearningActive = false;
        gLearningTarget = Command::NONE;
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
    gLearnedStore.begin();
    gController.begin(kSchedulerEnabled);

    if (kSchedulerEnabled) {
        loadDefaultSchedule();
    }
}

void loop() {
    const uint32_t nowMs = millis();

    mockHubInput(nowMs);
    mockBlynkInput(nowMs);

    processLearning(nowMs);
    if (gLearningActive) {
        // Learning mode is exclusive to avoid mixing capture with normal command handling.
        return;
    }

    processBlynkControl(nowMs);
    gController.tick(nowMs);
}
