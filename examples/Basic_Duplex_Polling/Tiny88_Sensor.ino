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
        const MyData& incoming = link.peek();
        
        if (incoming.commandId == 1) {
            digitalWrite(LED_BUILTIN, HIGH);
        }
        
        link.flush(); // Clear flag for next packet
    }

    // Send sensor data every 2s
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 2000) {
        MyData status = { millis(), 24.5f, 0 };
        link.send(TYPE_DATA, status);
        lastSend = millis();
    }
}
