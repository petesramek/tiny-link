#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

// --- The Callback (Replacing Polling Logic) ---
void handleIncoming(const MyData& data) {
    if (data.commandId == 1) {
        digitalWrite(LED_BUILTIN, HIGH);
    } else {
        digitalWrite(LED_BUILTIN, LOW);
    }
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Register the listener
    link.onReceive(handleIncoming);
}

void loop() {
    // Engine maintenance (Executes callback internally)
    link.update();

    // The rest of the logic (Sending) remains identical to Polling example
    static uint32_t lastSend = 0;
    if (millis() - lastSend > 2000) {
        MyData status = { millis(), 26.2f, 0 };
        link.send(TYPE_DATA, status);
        lastSend = millis();
    }
}
