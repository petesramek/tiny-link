/**
 * Auto_Update — Mode 1: Main-Loop Polling
 *
 * The simplest, most portable update mode.  No hardware interrupts are
 * required.  Call link.update() once per loop iteration; TinyLink handles
 * all framing, checksumming, and dispatching internally.
 *
 * Works on every Arduino-compatible board, including devices without any
 * hardware timer interrupt support (e.g. MH-Tiny88, ATtiny-class MCUs).
 *
 * Platform: any Arduino-compatible board
 * Wiring  : Serial TX → peer RX, Serial RX ← peer TX
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> link(adapter);

// ----- Callback (fires inside update()) ----------------------------------

void onReceive(const SensorData& d) {
    Serial.print(F("[RX] uptime="));
    Serial.print(d.uptime);
    Serial.print(F("  temp="));
    Serial.println(d.temperature);
}

// -------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onReceive);
    // No extra setup — just start using update() in the loop.
}

void loop() {
    // *** The only thing required to drive TinyLink ***
    link.update();

    // Send a reading every 2 seconds.
    static uint32_t lastSend = 0;
    static uint8_t  appSeq   = 0;
    if (millis() - lastSend > 2000) {
        SensorData msg = { millis(), 24.5f, appSeq++ };
        link.sendData(TYPE_DATA, msg);
        lastSend = millis();
    }
}
