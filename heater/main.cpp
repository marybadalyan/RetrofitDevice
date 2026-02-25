#include "../IRReciever.h"
#include "../logger.h"
#include "../prefferences.h"
#include "../time/wall_clock.h"
#include "heater.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
static inline uint32_t millis() { return 0; }
static inline uint32_t micros() { return 0; }
#endif

namespace {
IRReceiver gIrReceiver;
Logger gLogger;
WallClock gWallClock;
Heater gHeater;
RelayDriver gRelay;
DisplayDriver gDisplay;
CommandStatusLed gCommandStatusLed;
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    gIrReceiver.begin();
    gRelay.begin();
    gDisplay.begin();
    gCommandStatusLed.begin();

    gLogger.beginPersistence("heater-log");
    gWallClock.beginNtp(kNtpTimezone, kNtpServerPrimary, kNtpServerSecondary, kNtpServerTertiary);
}

void loop() {
    const uint32_t nowMs = millis();
    const uint32_t nowUs = micros();
    const WallClockSnapshot wallNow = gWallClock.now(nowMs, nowUs);

    DecodedFrame frame{};
    if (gIrReceiver.poll(frame)) {
        gLogger.log(wallNow, LogEventType::IR_FRAME_RX, frame.command, true, frame.isAck ? 1U : 0U);

        if (!frame.isAck) {
            const bool applied = gHeater.applyCommand(frame.command);
            gLogger.log(wallNow, LogEventType::STATE_CHANGE, frame.command, applied);
            if (applied) {
                gCommandStatusLed.showCommand(frame.command);
            }
        }
    }

    gRelay.setEnabled(gHeater.powerEnabled());
    gDisplay.showPowerState(gHeater.powerEnabled());
}
