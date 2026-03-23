/**
 * Auto_Update — Mode 2: Hardware Timer ISR
 *
 * Uses a hardware timer to call TinyLink::autoUpdateISR() at a fixed rate
 * (here: every 1 ms via the TimerOne library).  The main loop is free of any
 * TinyLink maintenance calls; the ISR drives the engine automatically.
 *
 * autoUpdateISR() is ready to use immediately after construction — no
 * enableAutoUpdate() call is needed for the standard single-instance setup.
 * Call enableAutoUpdate() only if you need to switch which of several same-type
 * instances is driven by the ISR.
 *
 * When to choose this mode:
 *   - The main loop has variable, unpredictable timing (busy-wait, delays).
 *   - You want deterministic, fixed-rate protocol maintenance.
 *   - A hardware timer is available (most Uno/Mega/ESP32/STM32 boards).
 *
 * Dependencies: TimerOne library (Arduino IDE Library Manager)
 *               https://github.com/PaulStoffregen/TimerOne
 *
 * Platform: Arduino Uno/Mega (AVR) and any board supported by TimerOne
 * Wiring  : Serial TX → peer RX, Serial RX ← peer TX
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include <TimerOne.h>          // Install via Library Manager
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> link(adapter);

// ----- Callback (fires inside the ISR-driven update()) -------------------

void onReceive(const SensorData& d) {
    // Keep ISR callbacks short — avoid Serial.print() inside an ISR on AVR.
    // Use a flag/buffer and process in loop() for safety on AVR targets.
    (void)d;
}

// -------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onReceive);

    // Attach the static ISR function to Timer1 (fires every 1 000 µs = 1 ms).
    // No enableAutoUpdate() required — the constructor already registered this
    // instance.
    Timer1.initialize(1000);
    Timer1.attachInterrupt(TinyLink<SensorData, TinyArduinoAdapter>::autoUpdateISR);
}

void loop() {
    // update() is NOT called here — the timer ISR handles it automatically.

    static uint32_t lastSend = 0;
    static uint8_t  appSeq   = 0;
    if (millis() - lastSend > 2000) {
        SensorData msg = { millis(), 24.5f, appSeq++ };
        link.sendData(TYPE_DATA, msg);
        lastSend = millis();
    }
}
