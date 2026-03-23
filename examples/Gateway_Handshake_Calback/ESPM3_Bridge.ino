#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> sensorLink(adapter);
TinyLink<GatewayStatus, TinyArduinoAdapter> statusLink(adapter);

// --- Callback: Process data when it arrives from Tiny88 ---
void handleSensorData(const SensorData& data) {
    // Forward to MQTT / Cloud
    // Serial.println(data.value);
}

void setup() {
    Serial.begin(9600);
    sensorLink.onDataReceived(handleSensorData);

    // 1. Clear any boot-up "UART noise"
    while(Serial.available()) Serial.read();

    // 2. Signal to the Tiny88 that we are ready to receive
    GatewayStatus msg = { 1, (uint32_t)millis() };
    statusLink.sendData(message_type_to_wire(MessageType::Cmd), msg);
}

void loop() {
    // Just keep the engine spinning
    sensorLink.update();
    statusLink.update();
}
