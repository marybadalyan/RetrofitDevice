#pragma once

#include <cstdint>
#include "commands.h"

struct LearnedCode {
    uint8_t  protocol;   // IRremote decode_type_t cast to uint8
    uint16_t address;
    uint16_t command;
};

enum class LearnPollResult : uint8_t {
    PENDING = 0,  // still waiting for a recognisable signal
    OK      = 1,  // signal captured and saved to NVS
    FAIL    = 2,  // no hardware available
};

// IRLearner: non-blocking IR-signal capture and NVS persistence.
//
// Usage (in the main loop):
//   learner.beginListen();
//   while (true) {
//       auto r = learner.poll(Command::ON_OFF);
//       if (r != LearnPollResult::PENDING) break;
//       if (timedOut) { learner.stopListen(); break; }
//   }
//
// Learned codes survive reboots via ESP32 NVS (Preferences).
class IRLearner {
public:
    void begin();           // call once in setup() — currently a no-op
    void beginListen();     // activate IrReceiver on kIrRxPin
    void stopListen();      // deactivate IrReceiver

    // Check once per loop iteration. If a frame arrived, saves it to NVS
    // for the given targetCmd and returns OK. Returns PENDING while waiting.
    // Returns FAIL only if called and no hardware is available.
    LearnPollResult poll(Command targetCmd);

    bool hasLearned(Command cmd) const;
    bool getCode(Command cmd, LearnedCode& out) const;
    void clearAll();

    // Retrieve the last captured code (valid after poll() returns OK).
    // Used by custom-button learning to send IR data back to the hub.
    bool getLastCaptured(LearnedCode& out) const;

    // ── Hardware send helpers (used by IRSender to avoid a second IRremote include) ──
    void beginSend();   // calls IrSender.begin(kIrTxPin)
    void sendCodeDirect(uint8_t protocol, uint16_t address, uint16_t command);

private:
    static const char* nvsPrefix(Command cmd);   // e.g. "on", "up", "dn"

    LearnedCode lastCaptured_{};
    bool        hasLastCaptured_ = false;
};
