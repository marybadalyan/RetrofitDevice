#include "../IRSender.h"
#include "../commands.h"

#if __has_include(<Arduino.h>)
#include <Arduino.h>
#else
#include <cstdint>
#endif

namespace {
IRSender gIrSender;

void printHelp() {
#if __has_include(<Arduino.h>)
    Serial.println("Remote controller ready.");
    Serial.println("Commands: on/off/up/down or 1/2/3/4");
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
}  // namespace

void setup() {
#if __has_include(<Arduino.h>)
    Serial.begin(115200);
#endif
    gIrSender.begin();
    printHelp();
}

void loop() {
#if __has_include(<Arduino.h>)
    if (!Serial.available()) {
        return;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();

    Command command = Command::NONE;
    if (!parseCommand(line, command)) {
        Serial.println("Unknown command. Use on/off/up/down or 1/2/3/4");
        return;
    }

    gIrSender.sendCommand(command);
    Serial.print("Sent: ");
    Serial.println(static_cast<int>(command));
#endif
}
