#include "../IRReciever.h"
#include "../IRSender.h"
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
IRSender gIrSender;
Logger gLogger;
WallClock gWallClock;
Heater gHeater;
RelayDriver gRelay;
DisplayDriver gDisplay;
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    gIrReceiver.begin();
    gIrSender.begin();
    gRelay.begin();
    gDisplay.begin();

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
                // Heater acknowledges command execution back to retrofit device.
                const TxFailureCode txResult = gIrSender.sendAck(frame.command);
                if (txResult == TxFailureCode::NONE) {
                    gLogger.log(wallNow, LogEventType::ACK_SENT, frame.command, true);
                } else {
                    gLogger.log(wallNow,
                                LogEventType::TRANSMIT_FAILED,
                                frame.command,
                                false,
                                static_cast<uint8_t>(txResult));
                }
            }
        }
    }

    gRelay.setEnabled(gHeater.powerEnabled());
    gDisplay.showPowerState(gHeater.powerEnabled());
}
