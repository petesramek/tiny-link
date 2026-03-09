#include "TinyLink.h"
#include "adapters/TinyArduinoAdapter.h"
#include "SharedData.h"

using namespace tinylink;

// Initialize on Hardware Serial
TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

// --- Asynchronous Callback ---
// This handles incoming sensor data from the MH-Tiny88
void onSensorDataReceived(const MyData& sensor) {
    // 1. Process the incoming data (e.g., Log to Serial or Cloud)
    Serial.print("[BRIDGE] Received Temp: ");
    Serial.print(sensor.temperature);
    Serial.print(" | Uptime: ");
    Serial.println(sensor.uptime);

    // 2. Respond immediately with a command back to the Sensor
    // This demonstrates the "Reactive" nature of callbacks
    MyData response = { millis(), 0.0f, 1 }; // Sending Command ID 1
    link.send(TYPE_DATA, response);
}

void setup() {
    // ESP-M3 uses 3.3V logic (Ensure level shifter on Tiny88 side!)
    Serial.begin(9600);

    // Register the callback
    link.onReceive(onSensorDataReceived);
    
    Serial.println("TinyLink v0.4.0 Callback Bridge Started.");
}

void loop() {
    // Maintain the engine. 
    // All decoding and callback execution happens inside here.
    link.update(); 

    // Note: No polling logic is needed here!
}
