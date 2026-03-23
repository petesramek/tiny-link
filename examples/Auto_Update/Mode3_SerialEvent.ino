/**
 * Auto_Update — Mode 3: serialEvent() (Zero-Interrupt Fallback)
 *
 * Arduino calls serialEvent() automatically between loop() iterations
 * whenever bytes are waiting in the hardware Serial RX FIFO.  This gives
 * near-immediate reaction to incoming data without needing a hardware timer
 * or UART RX interrupt.
 *
 * autoUpdateISR() is called from serialEvent() — the function name "ISR" is
 * kept for API consistency; serialEvent() is not a true hardware interrupt
 * on Arduino, but it provides equivalent zero-extra-step auto-update
 * behaviour on every Arduino board regardless of timer availability.
 *
 * When to choose this mode:
 *   - No hardware timer is available or all timers are already occupied.
 *   - You want zero-setup auto-update without any third-party library.
 *   - Targeting boards where timer interrupt APIs differ across cores
 *     (e.g., megaTinyCore, ATtinyCore).
 *
 * Limitation: serialEvent() is not called while the MCU is inside a blocking
 *   function (delay(), analogRead() >10 ms, etc.).  Keep loop() responsive.
 *
 * Platform: any Arduino-compatible board
 * Wiring  : Serial TX → peer RX, Serial RX ← peer TX
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorData, TinyArduinoAdapter> link(adapter);

// ----- Callback -----------------------------------------------------------

static volatile bool g_dataReady = false;
static SensorData    g_latest;

void onReceive(const SensorData& d) {
    g_latest    = d;
    g_dataReady = true;
}

// ----- Arduino serialEvent() — called between loop() iterations -----------
//
// No extra setup required; the constructor already registered the instance.
// Simply forward to autoUpdateISR() for API consistency and portability.
//
void serialEvent() {
    TinyLink<SensorData, TinyArduinoAdapter>::autoUpdateISR();
}

// -------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onReceive);
    // No enableAutoUpdate(), no timer setup — serialEvent() handles it all.
}

void loop() {
    // Process any data that arrived via serialEvent().
    if (g_dataReady) {
        g_dataReady = false;
        Serial.print(F("[RX] uptime="));
        Serial.print(g_latest.uptime);
        Serial.print(F("  temp="));
        Serial.println(g_latest.temperature);
    }

    static uint32_t lastSend = 0;
    static uint8_t  appSeq   = 0;
    if (millis() - lastSend > 2000) {
        SensorData msg = { millis(), 24.5f, appSeq++ };
        link.sendData(TYPE_DATA, msg);
        lastSend = millis();
    }
}
