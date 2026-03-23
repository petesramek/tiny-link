/**
 * IoT_Sensor_Gateway — Mode 1 (Polling / Main Loop)
 * Device: ATtiny88 — Temperature Sensor Node
 *
 * Responsibilities:
 *   1. Establish a TinyLink connection with the ESP8266 via begin().
 *   2. Wait for a TYPE_CMD request from the ESP8266.
 *   3. Acknowledge the request with sendAck() so the ESP8266 exits AWAITING_ACK.
 *   4. Read the temperature sensor and send back a TYPE_DATA response.
 *   5. Repeat every time the ESP8266 asks (nominally every 60 s).
 *
 * Update mode: link.update() called in loop() — works on every board,
 * no hardware interrupts or extra libraries required.
 *
 * Wiring (ATtiny88 @ 5 V  ↔  ESP8266 @ 3.3 V):
 *   ATtiny88 TX (PD1) → [1 kΩ]─┬─ ESP8266 RX
 *                                └─[2 kΩ]─ GND   (voltage divider)
 *   ATtiny88 RX (PD0) ──────────── ESP8266 TX     (3.3 V safe on Tiny88 input)
 *   GND ────────────────────────── GND
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);

// ---------------------------------------------------------------------------
// Sensor stub — replace with your real sensor library (e.g. DallasTemperature)
// ---------------------------------------------------------------------------
float readTemperature() {
    // Example: analogRead on ATtiny88 pin, scaled to °C
    return 21.5f;  // stub value
}

// ---------------------------------------------------------------------------
// Callback: fires when a TYPE_CMD request arrives from the ESP8266
// ---------------------------------------------------------------------------
void onRequest(const SensorMessage& req) {
    // Step 1: ACK immediately — releases the ESP8266 from AWAITING_ACK.
    link.sendAck();

    // Step 2: Read sensor and send the response back.
    SensorMessage resp;
    resp.requestId   = req.requestId;     // echo so ESP8266 can correlate
    resp.temperature = readTemperature();
    resp.uptime      = static_cast<uint32_t>(millis());

    link.sendData(TYPE_DATA, resp);
    // ATtiny88 now enters AWAITING_ACK; ESP8266 will call sendAck() when it
    // processes the data frame.
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onRequest);
    link.begin();   // Initiate TinyLink handshake with the ESP8266.
                    // Both sides call begin(); whoever boots first retries
                    // every 1 s until the other side answers.
}

void loop() {
    link.update();  // Drive the TinyLink engine.
                    // All application logic lives in the onRequest() callback.
}
