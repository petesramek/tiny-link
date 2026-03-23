/**
 * IoT_Sensor_Gateway — Mode 3 (serialEvent)
 * Device: ATtiny88 — Temperature Sensor Node
 *
 * Arduino calls serialEvent() automatically between loop() iterations
 * whenever bytes are waiting in the hardware Serial RX FIFO.  This gives
 * near-immediate reaction to incoming data without needing a hardware timer.
 *
 * serialEvent() is NOT a true hardware interrupt on Arduino:
 *   - It is called from the main-loop scheduler (between loop() calls).
 *   - Serial.write() and all other Arduino APIs are safe inside it.
 *   - Unlike Mode 1, you do NOT need to call link.update() in loop().
 *
 * Unlike Mode 2, callbacks fire in serialEvent() context (not ISR), so
 * sendAck() and sendData() can be called directly from the callback — no
 * volatile flags or deferred processing needed.
 *
 * Wiring: see IoT_Sensor_Gateway_Polling/ATtiny88_Sensor.ino
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);

// ---------------------------------------------------------------------------
// Sensor stub — replace with your real sensor library
// ---------------------------------------------------------------------------
float readTemperature() { return 21.5f; }

// ---------------------------------------------------------------------------
// Callback: fires from serialEvent() context — all Arduino APIs are safe here
// ---------------------------------------------------------------------------
void onRequest(const SensorMessage& req) {
    link.sendAck();                 // release ESP8266 from AWAITING_ACK

    SensorMessage resp;
    resp.requestId   = req.requestId;
    resp.temperature = readTemperature();
    resp.uptime      = static_cast<uint32_t>(millis());
    link.sendData(TYPE_DATA, resp); // ATtiny88 → AWAITING_ACK
}

// ---------------------------------------------------------------------------
// Arduino calls serialEvent() automatically when Serial RX bytes are waiting
// — no extra setup, no interrupt configuration, no third-party library.
// ---------------------------------------------------------------------------
void serialEvent() {
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR();
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onRequest);
    link.begin();   // Initiate TinyLink handshake.
}

void loop() {
    // Application logic only — no link.update() needed.
    // serialEvent() drives the TinyLink engine between loop() iterations.
}
