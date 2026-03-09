#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> sensorLink(adapter);
TinyLink<GatewayStatus, TinyArduinoAdapter> statusLink(adapter);

bool gatewayReady = false;

void onStatus(const GatewayStatus& status) {
    if (status.ready == 1) gatewayReady = true;
}

void setup() {
    Serial.begin(9600);
    statusLink.onReceive(onStatus);
    
    // 1. Wake up ESP (Assuming ESP_EN is on Pin 4)
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH); 
}

void loop() {
    // Keep both link engines updated (they share the same hardware adapter)
    statusLink.update();
    sensorLink.update();

    // 2. Wait for the "Ready" packet from ESP before sending
    if (gatewayReady) {
        SensorData myData = { 101, 23.5f };
        sensorLink.send(TYPE_DATA, myData);
        
        // 3. Optional: Go back to sleep after sending
        delay(100); // Ensure bytes clear the TX buffer
        digitalWrite(4, LOW); // Shutdown ESP
        gatewayReady = false;
        // Enter Low Power Sleep here...
    }
}
