#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"

using namespace tinylink;

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
        const tinylink::Stats stats = link.stats();
        
        Serial.println("--- TinyLink Health Report ---");
        Serial.print("Success Packets: "); Serial.println(stats.packetsReceived);
        Serial.print("Checksum Errors: "); Serial.println(stats.checksumMismatches);
        Serial.print("COBS Decode Failures: "); Serial.println(stats.cobsDecodeFailures);
        Serial.print("Inter-byte Timeouts: "); Serial.println(stats.interByteTimeouts);
        Serial.print("ACK Timeouts: "); Serial.println(stats.ackTimeouts);
        
        if (stats.checksumMismatches > 10 || stats.cobsDecodeFailures > 10) {
            Serial.println("WARNING: High interference detected!");
        }
        
        lastReport = millis();
    }
}
