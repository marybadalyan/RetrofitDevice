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
constexpr bool kUseSinglePowerButton = true;
constexpr int kPowerButtonPin = 18;
constexpr int kOnButtonPin = 18;
constexpr int kOffButtonPin = 19;
constexpr int kTempUpButtonPin = 21;
constexpr int kTempDownButtonPin = 22;
constexpr uint8_t kMaxButtons = 4;
constexpr uint32_t kButtonDebounceMs = 40;

enum class ButtonAction : uint8_t {
    COMMAND = 0,
    POWER_TOGGLE = 1,
};

struct ButtonState {
    int pin;
    ButtonAction action;
    Command command;
    const char* label;
    bool lastRawPressed;
    bool stablePressed;
    uint32_t lastRawChangeMs;
};

ButtonState gButtons[kMaxButtons];
uint8_t gButtonCount = 0;
bool gPowerExpectedOn = false;
#endif

bool sendWithLog(Command command, const char* source) {
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
        return false;
    }

    Serial.print("[REMOTE] TX ok source=");
    Serial.print(source);
    Serial.print(" cmd=");
    Serial.println(commandToString(command));
#else
    (void)source;
#endif
    return txResult == TxFailureCode::NONE;
}

#if __has_include(<Arduino.h>)
void updatePowerEstimate(Command command) {
    if (command == Command::ON) {
        gPowerExpectedOn = true;
        return;
    }
    if (command == Command::OFF) {
        gPowerExpectedOn = false;
    }
}

void addButton(int pin, ButtonAction action, Command command, const char* label) {
    if (gButtonCount >= kMaxButtons) {
        return;
    }

    ButtonState& button = gButtons[gButtonCount++];
    button.pin = pin;
    button.action = action;
    button.command = command;
    button.label = label;
    button.lastRawPressed = false;
    button.stablePressed = false;
    button.lastRawChangeMs = 0;
}

void configureButtons() {
    gButtonCount = 0;
    if (kUseSinglePowerButton) {
        addButton(kPowerButtonPin, ButtonAction::POWER_TOGGLE, Command::NONE, "POWER");
    } else {
        addButton(kOnButtonPin, ButtonAction::COMMAND, Command::ON, "ON");
        addButton(kOffButtonPin, ButtonAction::COMMAND, Command::OFF, "OFF");
    }
    addButton(kTempUpButtonPin, ButtonAction::COMMAND, Command::TEMP_UP, "TEMP_UP");
    addButton(kTempDownButtonPin, ButtonAction::COMMAND, Command::TEMP_DOWN, "TEMP_DOWN");
}
#endif

void printHelp() {
#if __has_include(<Arduino.h>)
    Serial.println("Remote controller ready.");
    Serial.println("Commands: on/off/up/down or 1/2/3/4");
    Serial.println("Buttons use INPUT_PULLUP (connect button to GND).");
    if (kUseSinglePowerButton) {
        Serial.print("POWER(toggle) -> GPIO");
        Serial.println(kPowerButtonPin);
    } else {
        Serial.print("ON -> GPIO");
        Serial.println(kOnButtonPin);
        Serial.print("OFF -> GPIO");
        Serial.println(kOffButtonPin);
    }
    Serial.print("TEMP_UP -> GPIO");
    Serial.println(kTempUpButtonPin);
    Serial.print("TEMP_DOWN -> GPIO");
    Serial.println(kTempDownButtonPin);
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
    configureButtons();
    for (uint8_t i = 0; i < gButtonCount; ++i) {
        ButtonState& button = gButtons[i];
        pinMode(button.pin, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(button.pin) == LOW);
        button.lastRawPressed = rawPressed;
        button.stablePressed = rawPressed;
        button.lastRawChangeMs = millis();
    }
}

void pollButtons() {
    const uint32_t nowMs = millis();
    for (uint8_t i = 0; i < gButtonCount; ++i) {
        ButtonState& button = gButtons[i];
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

        Command command = button.command;
        if (button.action == ButtonAction::POWER_TOGGLE) {
            command = gPowerExpectedOn ? Command::OFF : Command::ON;
        }

        Serial.print("[REMOTE] Button ");
        Serial.print(button.label);
        Serial.print(" pressed on GPIO");
        Serial.print(button.pin);
        Serial.print(" cmd=");
        Serial.println(commandToString(command));

        if (sendWithLog(command, "BUTTON")) {
            updatePowerEstimate(command);
        }
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
        if (sendWithLog(command, "SERIAL")) {
            updatePowerEstimate(command);
        }
    }
#endif
}
