#include <../../src/TinyLink.h>
#include <../../src/adapters/TinyArduinoAdapter.h>

struct Dummy { uint32_t val; };
TinyArduinoAdapter hw(Serial);
TinyLink<Dummy, TinyArduinoAdapter> link(hw);

void setup() {
    Serial.begin(9600);
}

void loop() {
    link.update();

    static uint32_t lastReport = 0;
    if (millis() - lastReport > 5000) {
        const TinyStats& stats = link.getStats();
        
        Serial.println("--- TinyLink Health Report ---");
        Serial.print("Success Packets: "); Serial.println(stats.packets);
        Serial.print("CRC/COBS Errors: "); Serial.println(stats.crcErrs);
        Serial.print("Timeouts:        "); Serial.println(stats.timeouts);
        
        if (stats.crcErrs > 10) {
            Serial.println("WARNING: High interference detected!");
        }
        
        lastReport = millis();
    }
}
