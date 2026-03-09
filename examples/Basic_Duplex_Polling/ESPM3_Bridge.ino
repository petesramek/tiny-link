#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

void setup() {
    // ESP-M3 uses Serial at 3.3V logic
    Serial.begin(9600);
}

void loop() {
    link.update();

    if (link.available()) {
        const MyData& sensor = link.peek();
        // Here you would forward 'sensor.temperature' to MQTT or Web
        
        // Respond with a command
        MyData cmd = { millis(), 0.0f, 1 };
        link.send(TYPE_DATA, cmd);
        
        link.flush();
    }
}
