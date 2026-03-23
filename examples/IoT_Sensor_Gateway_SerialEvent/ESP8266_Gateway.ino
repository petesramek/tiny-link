/**
 * IoT_Sensor_Gateway — Mode 3 (serialEvent)
 * Device: ESP8266 — IoT Gateway
 *
 * Arduino calls serialEvent() automatically between loop() iterations
 * whenever bytes are waiting in the hardware Serial RX FIFO.  This gives
 * near-immediate RX processing without a hardware timer or third-party library.
 *
 * serialEvent() is NOT a true hardware interrupt:
 *   - It runs between loop() iterations, in the normal execution context.
 *   - All Arduino and ESP8266 APIs (WiFi, HTTP, Serial) are safe inside it.
 *   - The callback can call sendAck() and sendData() directly.
 *
 * Limitation: serialEvent() is NOT called while loop() is inside a blocking
 *   function (delay(), HTTP POST, WiFi setup, etc.).  The 60-second HTTP POST
 *   is fine here because it is short enough in practice; for very slow
 *   endpoints consider Mode 2 (Timer ISR) instead.
 *
 * Wiring: see IoT_Sensor_Gateway_Polling/ATtiny88_Sensor.ino
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "SharedData.h"

using namespace tinylink;

// ---------------------------------------------------------------------------
// Configuration — update before uploading
// ---------------------------------------------------------------------------
const char* WIFI_SSID    = "YourSSID";
const char* WIFI_PASS    = "YourPassword";
const char* ENDPOINT_URL = "http://api.example.com/temperature";

const unsigned long REQUEST_INTERVAL_MS = 60000UL;

// ---------------------------------------------------------------------------

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);

static uint8_t       requestId   = 0;
static unsigned long lastRequest = 0;

// ---------------------------------------------------------------------------
// Send reading to the cloud
// ---------------------------------------------------------------------------
void postToCloud(const SensorMessage& data) {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClient  client;
    HTTPClient  http;
    http.begin(client, ENDPOINT_URL);
    http.addHeader("Content-Type", "application/json");

    char body[96];
    snprintf(body, sizeof(body),
             "{\"id\":%u,\"temp\":%.2f,\"uptime\":%lu}",
             (unsigned)data.requestId,
             (double)data.temperature,
             (unsigned long)data.uptime);

    int code = http.POST(body);
    http.end();
    (void)code;
}

// ---------------------------------------------------------------------------
// Callback: fires from serialEvent() context — all APIs are safe here
// ---------------------------------------------------------------------------
void onSensorData(const SensorMessage& data) {
    link.sendAck();     // release ATtiny88 from AWAITING_ACK
    postToCloud(data);  // forward reading to the cloud
}

// ---------------------------------------------------------------------------
// Arduino calls serialEvent() automatically when Serial RX bytes are waiting
// ---------------------------------------------------------------------------
void serialEvent() {
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR();
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(100); }

    link.onDataReceived(onSensorData);
    link.begin();   // Initiate TinyLink handshake.
}

void loop() {
    // Every 60 s (once the link is up): request a sensor reading.
    if (link.connected() &&
            (millis() - lastRequest >= REQUEST_INTERVAL_MS)) {
        SensorMessage req;
        req.requestId   = requestId++;
        req.temperature = 0.0f;
        req.uptime      = static_cast<uint32_t>(millis());
        link.sendData(TYPE_CMD, req);
        lastRequest = millis();
    }
}
