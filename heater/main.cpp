#include "heater/heater.h"
#include "IRReciever.h"
#include "logger.h"
#include "time/wall_clock.h"
#include <Arduino.h>

#ifdef REAL_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

namespace {
    IRReceiver  gIrReceiver;
    Heater      gHeater;
    Logger      gLogger;
    NtpClock    gWallClock;

#ifdef REAL_OLED
    Adafruit_SSD1306 gDisplay(128, 64, &Wire, -1);
#endif

    // For OLED display
    Command  gLastCmd      = Command::NONE;
    float    gSetpointC    = 21.0f;
}

#ifdef REAL_OLED
void updateDisplay() {
    gDisplay.clearDisplay();
    gDisplay.setTextColor(SSD1306_WHITE);

    // Row 1: power state large
    gDisplay.setTextSize(2);
    gDisplay.setCursor(0, 0);
    gDisplay.print(gHeater.powerEnabled() ? "ON" : "OFF");

    // Row 2: setpoint
    gDisplay.setTextSize(1);
    gDisplay.setCursor(0, 20);
    gDisplay.printf("Setpoint: %.1fC", gSetpointC);

    // Row 3: last command received
    gDisplay.setCursor(0, 32);
    gDisplay.printf("Last IR: %s", commandToString(gLastCmd));

    // Row 4: GPIO info
    gDisplay.setCursor(0, 44);
    gDisplay.printf("RX pin: GPIO%d", kIrRxPin);

    gDisplay.display();
}
#endif

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[HEATER] Booting...");

    gIrReceiver.begin();
    Serial.printf("[IR-RX] Listening on GPIO %d\n", kIrRxPin);

#ifdef REAL_OLED
    Wire.begin(kOledSdaPin, kOledSclPin);
    if (!gDisplay.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
        Serial.println("[OLED] Not found!");
    } else {
        gDisplay.clearDisplay();
        gDisplay.setTextSize(1);
        gDisplay.setTextColor(SSD1306_WHITE);
        gDisplay.setCursor(0, 0);
        gDisplay.println("ThermoHub");
        gDisplay.println("Heater unit");
        gDisplay.println("Waiting for IR...");
        gDisplay.display();
        Serial.println("[OLED] Ready.");
    }
#endif

    Serial.println("[HEATER] Ready — waiting for IR commands.");
}

void loop() {
    DecodedFrame frame;
    if (gIrReceiver.poll(frame)) {
        // Log to terminal
        Serial.printf("[IR-RX] Command received: %s\n", commandToString(frame.command));

        // Apply to heater
        const bool applied = gHeater.applyCommand(frame.command);

        // Update setpoint tracking for display
        if (frame.command == Command::TEMP_UP)   gSetpointC += 0.5f;
        if (frame.command == Command::TEMP_DOWN)  gSetpointC -= 0.5f;
        gLastCmd = frame.command;

        // Log result
        Serial.printf("[HEATER] %s — power=%s setpoint=%.1fC\n",
                      applied ? "Applied" : "Ignored",
                      gHeater.powerEnabled() ? "ON" : "OFF",
                      gSetpointC);

        // LED feedback
        CommandStatusLed led;
        led.begin();
        led.showCommand(frame.command);

#ifdef REAL_OLED
        updateDisplay();
#endif
    }
}