#include <TinyLink.h>
#include <../src/adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

// Initialize Adapter on Hardware Serial
TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

void onCommandReceived(const MyData& data) {
    // If ESP sends a command back
    if (data.commandId == 1) {
        digitalWrite(LED_BUILTIN, HIGH);
    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Register asynchronous callback
    link.onReceive(onCommandReceived);
}

void loop() {
    link.update();

    static uint32_t lastSend = 0;
    if (millis() - lastSend > 2000) {
        MyData status = { millis(), 24.5f, 0 };
        link.send(TYPE_DATA, status);
        lastSend = millis();
    }
}
