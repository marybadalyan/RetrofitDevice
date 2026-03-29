#include "IRLearner.h"
#include "prefferences.h"

#if __has_include(<Arduino.h>)
#define RECORD_GAP_MICROS 5000
#include <IRremote.hpp>
#include <Preferences.h>
#define IRLEARNER_HW 1
#else
#define IRLEARNER_HW 0
#endif

// ── NVS helpers ───────────────────────────────────────────────────────────────
// NVS namespace is "ir-learn" (≤15 chars).
// Keys per command: "<pfx>_prot", "<pfx>_addr", "<pfx>_cmd", "<pfx>_raw",
//                  "<pfx>_rlen", "<pfx>_rdat"  (all ≤15 chars).

const char* IRLearner::nvsPrefix(Command cmd) {
    switch (cmd) {
        case Command::ON_OFF:    return "on";
        case Command::TEMP_UP:   return "up";
        case Command::TEMP_DOWN: return "dn";
        default:                 return nullptr;
    }
}

#if IRLEARNER_HW
static void buildKey(char* buf, size_t bufLen,
                     const char* prefix, const char* suffix) {
    snprintf(buf, bufLen, "%s_%s", prefix, suffix);
}

static void saveCode(const char* pfx, const LearnedCode& code) {
    Preferences prefs;
    prefs.begin("ir-learn", false);
    char key[16];

    buildKey(key, sizeof(key), pfx, "prot");
    prefs.putUChar(key, code.protocol);

    buildKey(key, sizeof(key), pfx, "addr");
    prefs.putUShort(key, code.address);

    buildKey(key, sizeof(key), pfx, "cmd");
    prefs.putUShort(key, code.command);

    prefs.end();
}

static bool loadCode(const char* pfx, LearnedCode& out) {
    Preferences prefs;
    prefs.begin("ir-learn", true);   // read-only
    char key[16];

    buildKey(key, sizeof(key), pfx, "prot");
    if (!prefs.isKey(key)) {
        prefs.end();
        return false;   // never learned
    }

    out.protocol = prefs.getUChar(key, 0);

    buildKey(key, sizeof(key), pfx, "addr");
    out.address = prefs.getUShort(key, 0);

    buildKey(key, sizeof(key), pfx, "cmd");
    out.command = prefs.getUShort(key, 0);

    prefs.end();
    return true;
}

static void clearPrefix(const char* pfx) {
    Preferences prefs;
    prefs.begin("ir-learn", false);
    char key[16];

    const char* suffixes[] = {"prot", "addr", "cmd"};
    for (const char* s : suffixes) {
        buildKey(key, sizeof(key), pfx, s);
        prefs.remove(key);
    }
    prefs.end();
}
#endif  // IRLEARNER_HW

// ── IRLearner methods ──────────────────────────────────────────────────────────

void IRLearner::begin() {
    // Receiver is started on-demand in beginListen().
    // Call beginSend() separately (after construction) to initialise the transmitter.
}

void IRLearner::beginSend() {
#if IRLEARNER_HW
    // Initialise both sender and receiver once at boot.
    // On ESP32, IrSender and IrReceiver share a hardware timer — both must be
    // started together. The receiver is then stopped until a learn session begins.
    IrSender.begin(kIrTxPin);
    IrReceiver.begin(kIrRxPin, DISABLE_LED_FEEDBACK);
    IrReceiver.stop();   // keep it paused until beginListen() is called
    Serial.printf("[LEARN] IR hardware ready — TX GPIO %d  RX GPIO %d\n",
                  kIrTxPin, kIrRxPin);
#endif
}

void IRLearner::sendCodeDirect(uint8_t protocol, uint16_t address, uint16_t command) {
#if IRLEARNER_HW
    IrSender.write(static_cast<decode_type_t>(protocol), address, command, 0);
#else
    (void)protocol; (void)address; (void)command;
#endif
}

void IRLearner::beginListen() {
#if IRLEARNER_HW
    IrReceiver.begin(kIrRxPin, DISABLE_LED_FEEDBACK);
    Serial.printf("[LEARN] Listening on GPIO %d…\n", kIrRxPin);
#endif
}

void IRLearner::stopListen() {
#if IRLEARNER_HW
    IrReceiver.stop();
    Serial.println("[LEARN] Stopped listening.");
#endif
}

LearnPollResult IRLearner::poll(Command targetCmd) {
#if IRLEARNER_HW
    if (!IrReceiver.decode()) {
        return LearnPollResult::PENDING;
    }

    const IRData& d = IrReceiver.decodedIRData;
    const bool isRepeat = (d.flags & IRDATA_FLAGS_IS_REPEAT) != 0;

    if (isRepeat) {
        IrReceiver.resume();
        return LearnPollResult::PENDING;
    }

    if (d.protocol == UNKNOWN) {
        // IRremote couldn't identify the protocol — skip and keep listening.
        // All mainstream heater remotes (NEC, Samsung, LG, Panasonic, etc.)
        // are decoded by IRremote as named protocols, so this only discards
        // truly exotic or garbled signals.
        Serial.println("[LEARN] Unknown protocol — ignoring, keep listening…");
        IrReceiver.resume();
        return LearnPollResult::PENDING;
    }

    // Named protocol — store compactly (survives reboots, tiny NVS footprint)
    LearnedCode code{};
    code.protocol = static_cast<uint8_t>(d.protocol);
    code.address  = d.address;
    code.command  = d.command;
    Serial.printf("[LEARN] Got protocol %d addr=0x%04X cmd=0x%04X\n",
                  code.protocol, code.address, code.command);

    IrReceiver.resume();

    // Always store the last captured code (used by custom-button learning)
    lastCaptured_    = code;
    hasLastCaptured_ = true;

    const char* pfx = nvsPrefix(targetCmd);
    if (pfx) {
        saveCode(pfx, code);
        Serial.printf("[LEARN] Saved to NVS under prefix '%s'\n", pfx);
    }

    return LearnPollResult::OK;
#else
    (void)targetCmd;
    return LearnPollResult::FAIL;
#endif
}

bool IRLearner::hasLearned(Command cmd) const {
#if IRLEARNER_HW
    const char* pfx = nvsPrefix(cmd);
    if (!pfx) return false;
    Preferences prefs;
    prefs.begin("ir-learn", true);
    char key[16];
    buildKey(key, sizeof(key), pfx, "prot");
    const bool exists = prefs.isKey(key);
    prefs.end();
    return exists;
#else
    (void)cmd;
    return false;
#endif
}

bool IRLearner::getCode(Command cmd, LearnedCode& out) const {
#if IRLEARNER_HW
    const char* pfx = nvsPrefix(cmd);
    if (!pfx) return false;
    return loadCode(pfx, out);
#else
    (void)cmd; (void)out;
    return false;
#endif
}

void IRLearner::clearAll() {
#if IRLEARNER_HW
    const Command cmds[] = { Command::ON_OFF, Command::TEMP_UP, Command::TEMP_DOWN };
    for (Command c : cmds) {
        const char* pfx = nvsPrefix(c);
        if (pfx) clearPrefix(pfx);
    }
    Serial.println("[LEARN] All learned codes cleared from NVS.");
#endif
}

bool IRLearner::getLastCaptured(LearnedCode& out) const {
    if (!hasLastCaptured_) return false;
    out = lastCaptured_;
    return true;
}
