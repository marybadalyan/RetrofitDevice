#include "../IRSender.h"
#include "../commands.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
#endif

namespace {
IRSender gIrSender;

#if __has_include(<Arduino.h>)
constexpr int kOnButtonPin = 18;
constexpr int kOffButtonPin = 19;
constexpr uint32_t kButtonDebounceMs = 40;

struct ButtonState {
    int pin = -1;
    Command command = Command::NONE;
    const char* label = "";
    bool lastRawPressed = false;
    bool stablePressed = false;
    uint32_t lastRawChangeMs = 0;
};

ButtonState gButtons[] = {
    {kOnButtonPin, Command::ON, "ON", false, false, 0},
    {kOffButtonPin, Command::OFF, "OFF", false, false, 0},
};
#endif

void sendWithLog(Command command, const char* source) {
#if __has_include(<Arduino.h>)
    Serial.print("[REMOTE] TX request source=");
    Serial.print(source);
    Serial.print(" cmd=");
    Serial.println(commandToString(command));
#endif

    const TxFailureCode txResult = gIrSender.sendCommand(command);

#if __has_include(<Arduino.h>)
    if (txResult != TxFailureCode::NONE) {
        Serial.print("[REMOTE] TX failed source=");
        Serial.print(source);
        Serial.print(" cmd=");
        Serial.print(commandToString(command));
        Serial.print(" code=");
        Serial.println(static_cast<int>(txResult));
        return;
    }

    Serial.print("[REMOTE] TX ok source=");
    Serial.print(source);
    Serial.print(" cmd=");
    Serial.println(commandToString(command));
#else
    (void)source;
    (void)txResult;
#endif
}

void printHelp() {
#if __has_include(<Arduino.h>)
    Serial.println("Remote controller ready.");
    Serial.println("Commands: on/off/up/down or 1/2/3/4");
    Serial.println("Button wiring: ON->GPIO18 to GND, OFF->GPIO19 to GND (INPUT_PULLUP).");
#endif
}

#if __has_include(<Arduino.h>)
bool parseCommand(const String& line, Command& outCommand) {
    if (line.equalsIgnoreCase("1") || line.equalsIgnoreCase("on")) {
        outCommand = Command::ON;
        return true;
    }
    if (line.equalsIgnoreCase("2") || line.equalsIgnoreCase("off")) {
        outCommand = Command::OFF;
        return true;
    }
    if (line.equalsIgnoreCase("3") || line.equalsIgnoreCase("up")) {
        outCommand = Command::TEMP_UP;
        return true;
    }
    if (line.equalsIgnoreCase("4") || line.equalsIgnoreCase("down")) {
        outCommand = Command::TEMP_DOWN;
        return true;
    }
    return false;
}
#endif

#if __has_include(<Arduino.h>)
void setupButtons() {
    for (ButtonState& button : gButtons) {
        pinMode(button.pin, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(button.pin) == LOW);
        button.lastRawPressed = rawPressed;
        button.stablePressed = rawPressed;
        button.lastRawChangeMs = millis();
    }
}

void pollButtons() {
    const uint32_t nowMs = millis();
    for (ButtonState& button : gButtons) {
        const bool rawPressed = (digitalRead(button.pin) == LOW);
        if (rawPressed != button.lastRawPressed) {
            button.lastRawPressed = rawPressed;
            button.lastRawChangeMs = nowMs;
        }

        if ((nowMs - button.lastRawChangeMs) < kButtonDebounceMs) {
            continue;
        }
        if (button.stablePressed == rawPressed) {
            continue;
        }

        button.stablePressed = rawPressed;
        if (!button.stablePressed) {
            continue;
        }

        Serial.print("[REMOTE] Button ");
        Serial.print(button.label);
        Serial.print(" pressed on GPIO");
        Serial.println(button.pin);
        sendWithLog(button.command, "BUTTON");
    }
}
#endif
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    gIrSender.begin();
#if __has_include(<Arduino.h>)
    setupButtons();
#endif
    printHelp();
}

void loop() {
#if __has_include(<Arduino.h>)
    pollButtons();

    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();

        Command command = Command::NONE;
        if (!parseCommand(line, command)) {
            Serial.println("Unknown command. Use on/off/up/down or 1/2/3/4");
            return;
        }
        sendWithLog(command, "SERIAL");
    }
#endif
}
