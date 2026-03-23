/**
 * IoT_Sensor_Gateway — Mode 1 (Polling / Main Loop)
 * Device: ESP8266 — IoT Gateway
 *
 * Responsibilities:
 *   1. Connect to Wi-Fi.
 *   2. Establish a TinyLink connection with the ATtiny88 via begin().
 *   3. Every 60 seconds, send a TYPE_CMD request to the ATtiny88.
 *   4. When the ATtiny88 sends back a TYPE_DATA response, acknowledge it
 *      with sendAck() and forward the temperature reading to the cloud.
 *
 * Update mode: link.update() called in loop() — portable, zero extra setup.
 *
 * Wiring: see ATtiny88_Sensor.ino
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

const unsigned long REQUEST_INTERVAL_MS = 60000UL;  // 60 s between requests

// ---------------------------------------------------------------------------

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);

static uint8_t       requestId   = 0;
static unsigned long lastRequest = 0;

// ---------------------------------------------------------------------------
// Send the temperature reading to the cloud endpoint
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

    int code = http.POST(body);  // blocks until response or timeout
    http.end();
    (void)code;  // log or handle error code as needed
}

// ---------------------------------------------------------------------------
// Callback: fires when a TYPE_DATA response arrives from the ATtiny88
// ---------------------------------------------------------------------------
void onSensorData(const SensorMessage& data) {
    // Step 1: ACK the response — releases the ATtiny88 from AWAITING_ACK.
    link.sendAck();

    // Step 2: Forward the reading to the cloud.
    postToCloud(data);
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(100); }

    link.onDataReceived(onSensorData);
    link.begin();   // Initiate TinyLink handshake with the ATtiny88.
}

void loop() {
    link.update();  // Drive the TinyLink engine.

    // Every 60 s (once the link is up): request a sensor reading.
    if (link.connected() &&
            (millis() - lastRequest >= REQUEST_INTERVAL_MS)) {

        SensorMessage req;
        req.requestId   = requestId++;
        req.temperature = 0.0f;   // not used in requests
        req.uptime      = static_cast<uint32_t>(millis());

        link.sendData(TYPE_CMD, req);   // → ATtiny88 receives this as a Cmd
        lastRequest = millis();
    }
}
