#include "heater/heater.h"
#include "logger.h"
#include "time/wall_clock.h"
#include "prefferences.h"
#include <Arduino.h>

#ifdef REAL_IR_RX
#include "IRReciever.h"
#endif

#ifdef REAL_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

// ── GLOBALS ──────────────────────────────────────────────────
namespace {
    Heater           gHeater;
    Logger           gLogger;
    CommandStatusLed gStatusLed;

    Command gLastCmd   = Command::NONE;
    float   gSetpointC = 21.0f;

#ifdef REAL_IR_RX
    IRReceiver gIrReceiver;
#endif

#ifdef REAL_OLED
    Adafruit_SSD1306 gDisplay(128, 64, &Wire, -1);
#endif
} // namespace

// ── OLED UPDATE ───────────────────────────────────────────────
#ifdef REAL_OLED
void updateDisplay() {
    gDisplay.clearDisplay();
    gDisplay.setTextColor(SSD1306_WHITE);

    gDisplay.setTextSize(2);
    gDisplay.setCursor(0, 0);
    gDisplay.print(gHeater.powerEnabled() ? "ON" : "OFF");

    gDisplay.setTextSize(1);
    gDisplay.setCursor(0, 20);
    gDisplay.printf("Setpoint: %.1fC", gSetpointC);

    gDisplay.setCursor(0, 32);
    gDisplay.printf("Last IR: %s", commandToString(gLastCmd));

    gDisplay.setCursor(0, 44);
#ifdef REAL_IR_RX
    gDisplay.printf("RX GPIO%d active", kIrRxPin);
#else
    gDisplay.print("RX: not connected");
#endif

    gDisplay.display();
}
#endif

// ── APPLY COMMAND ─────────────────────────────────────────────
void applyCommand(Command cmd) {
    Serial.printf("[IR-RX] Command received: %s\n", commandToString(cmd));

    bool applied = true;
    bool isOn = gHeater.powerEnabled();

    if (cmd == Command::ON_OFF)
    {
        gHeater.setPowerEnabled(!isOn);
    }
    else if (!isOn)
    {
        applied = false;
    }
    else if (cmd == Command::TEMP_UP)
    {
        gSetpointC += 0.5f;
    }
    else if (cmd == Command::TEMP_DOWN)
    {
        gSetpointC -= 0.5f;
    }
    else
    {
        applied = false;
    }
   
    gLastCmd = cmd;

    Serial.printf("[HEATER] %s — power=%s setpoint=%.1fC\n",
                  applied ? "Applied" : "Ignored",
                  gHeater.powerEnabled() ? "ON" : "OFF",
                  gSetpointC);

    // ON=Green  OFF=Red  TEMP_UP=Blue  TEMP_DOWN=Yellow
    gStatusLed.showCommand(cmd,gHeater.powerEnabled());

#ifdef REAL_OLED
    updateDisplay();
#endif
}

// ── SETUP ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("[HEATER] Booting...");

    gStatusLed.begin();
    Serial.printf("[LED] Ready (R=%d G=%d B=%d)\n",
                  kStatusLedRedPin, kStatusLedGreenPin, kStatusLedBluePin);

#ifdef REAL_IR_RX
    gIrReceiver.begin();
    Serial.printf("[IR-RX] Listening on GPIO %d\n", kIrRxPin);
#else
    Serial.println("[IR-RX] Not connected — add -DREAL_IR_RX to enable");
#endif

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
#ifdef REAL_IR_RX
        gDisplay.println("Waiting for IR...");
#else
        gDisplay.println("IR RX not enabled");
#endif
        gDisplay.display();
        Serial.println("[OLED] Ready.");
    }
#endif

    Serial.println("[HEATER] Ready.");
}

// ── LOOP ─────────────────────────────────────────────────────
void loop() {
#ifdef REAL_IR_RX
    DecodedFrame frame;
    if (gIrReceiver.poll(frame)) {
        applyCommand(frame.command);
    }
#endif
}