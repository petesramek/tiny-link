#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

void setup() {
    Serial.begin(9600);
}

void loop() {
    link.update();

    // Polling for new data
    if (link.available()) {
        const MyData incoming = link.read();
        
        if (incoming.commandId == 1) {
            digitalWrite(LED_BUILTIN, HIGH);
        }
        
    }

    // Send sensor data every 2s
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 2000) {
        MyData status = { millis(), 24.5f, 0 };
        link.sendData(static_cast<uint8_t>('D'), status);
        lastSend = millis();
    }
}
