#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> sensorLink(adapter);

void setup() {
    Serial.begin(9600);
    
    // 1. Wait for Wi-Fi to connect (Simplified)
    // WiFi.begin(...); while (WiFi.status() != WL_CONNECTED) { delay(100); }

    // 2. Clear any bootloader garbage from the RX buffer
    while(Serial.available()) Serial.read();

    // 3. Signal to Tiny88 that we are stable and ready for data
    TinyLink<GatewayStatus, TinyArduinoAdapter> statusLink(adapter);
    GatewayStatus msg = { 1, 0 };
    statusLink.sendData(message_type_to_wire(MessageType::Cmd), msg);
}

void loop() {
    sensorLink.update();
    if (sensorLink.available()) {
        const SensorData& d = sensorLink.peek();
        // Forward to Cloud...
        sensorLink.flush();
    }
}
