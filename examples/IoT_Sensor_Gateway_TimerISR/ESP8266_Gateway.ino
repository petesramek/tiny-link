/**
 * IoT_Sensor_Gateway — Mode 2 (Hardware Timer ISR)
 * Device: ESP8266 — IoT Gateway
 *
 * This sketch is functionally identical to the Polling version but uses the
 * ESP8266's hardware Ticker to call autoUpdateISR() every 1 ms.  The main
 * loop() is completely free of TinyLink maintenance calls.
 *
 * ⚠ ISR Safety Rule
 *   On ESP8266, Ticker callbacks run in a hardware interrupt context.
 *   Calling Serial.write() or WiFi/HTTP APIs from inside a Ticker callback
 *   is NOT safe.  The callback only sets volatile flags; all network and
 *   Serial operations happen in loop().
 *
 * Dependencies: Ticker (built into the ESP8266 Arduino core — no install needed)
 *
 * Wiring: see IoT_Sensor_Gateway_Polling/ATtiny88_Sensor.ino
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
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

Ticker tinyLinkTicker;  // drives autoUpdateISR() every 1 ms

// ---------------------------------------------------------------------------
// Deferred sensor-data state — written in ISR context, consumed in loop()
// ---------------------------------------------------------------------------
static volatile bool     g_dataReady   = false;
static volatile uint8_t  g_receivedId  = 0;
static volatile float    g_receivedTemp= 0.0f;
static volatile uint32_t g_receivedUp  = 0;

static uint8_t       requestId   = 0;
static unsigned long lastRequest = 0;

// ---------------------------------------------------------------------------
// Send reading to the cloud (called from loop — not ISR safe)
// ---------------------------------------------------------------------------
void postToCloud(uint8_t id, float temp, uint32_t uptime) {
    if (WiFi.status() != WL_CONNECTED) return;

    WiFiClient  client;
    HTTPClient  http;
    http.begin(client, ENDPOINT_URL);
    http.addHeader("Content-Type", "application/json");

    char body[96];
    snprintf(body, sizeof(body),
             "{\"id\":%u,\"temp\":%.2f,\"uptime\":%lu}",
             (unsigned)id, (double)temp, (unsigned long)uptime);

    int code = http.POST(body);
    http.end();
    (void)code;
}

// ---------------------------------------------------------------------------
// Callback: fires from inside the Ticker-driven autoUpdateISR()
// ⚠ Ticker is hardware-interrupt context on ESP8266 — only set flags here.
// ---------------------------------------------------------------------------
void onSensorData(const SensorMessage& data) {
    g_receivedId   = data.requestId;
    g_receivedTemp = data.temperature;
    g_receivedUp   = data.uptime;
    g_dataReady    = true;
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(100); }

    link.onDataReceived(onSensorData);
    link.begin();  // Initiate TinyLink handshake.

    // Attach the static ISR function — no enableAutoUpdate() needed;
    // the constructor already registered this instance.
    tinyLinkTicker.attach_ms(1,
        TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR);
}

void loop() {
    // Process received sensor data (deferred from ISR context).
    if (g_dataReady) {
        g_dataReady = false;                           // clear first (re-entry safe)
        link.sendAck();                                // release ATtiny88 from AWAITING_ACK
        postToCloud(g_receivedId, g_receivedTemp, g_receivedUp);
    }

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
