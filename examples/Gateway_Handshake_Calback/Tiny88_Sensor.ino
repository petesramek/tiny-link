#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include <avr/sleep.h> // For MH-Tiny88 power saving
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> sensorLink(adapter);
TinyLink<GatewayStatus, TinyArduinoAdapter> statusLink(adapter);

bool espIsReady = false;
unsigned long wakeTime = 0;
const unsigned long ESP_TIMEOUT = 5000; // 5s Max wait for ESP boot

// --- Callback: Triggered when ESP sends the "Ready" packet ---
void onGatewayReady(const GatewayStatus& status) {
    if (status.ready == 1) {
        espIsReady = true;
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(4, OUTPUT); // ESP_EN Pin

    // Register the asynchronous listener
    statusLink.onReceive(onGatewayReady);

    // Initial Trigger: Power on the ESP
    digitalWrite(4, HIGH); 
    wakeTime = millis();
}

void loop() {
    // 1. Maintain both engines
    statusLink.update();
    sensorLink.update();

    // 2. Action: ESP signaled it is ready via Callback
    if (espIsReady) {
        SensorData myData = { 101, 23.5f };
        sensorLink.send(TYPE_DATA, myData);
        
        // Success! Blink and Shutdown
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100); // Allow TX buffer to clear
        digitalWrite(4, LOW); // Power off ESP
        
        espIsReady = false;
        // Proceed to Deep Sleep or wait for next interval...
    }

    // 3. Safety Guard: ESP failed to boot or signal
    if (!espIsReady && (millis() - wakeTime > ESP_TIMEOUT)) {
        digitalWrite(4, LOW); // Don't waste battery on a hung ESP
        // Log Error or Retry later
        wakeTime = millis(); // Reset timer for next attempt
    }
}
