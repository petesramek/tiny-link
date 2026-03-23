/**
 * IoT_Sensor_Gateway — Mode 2 (Hardware Timer ISR)
 * Device: ATtiny88 — Temperature Sensor Node
 *
 * This sketch is functionally identical to the Polling version but drives
 * the TinyLink engine from a hardware timer interrupt instead of loop().
 * The main loop is free of any TinyLink maintenance calls; the ISR handles
 * all RX processing, timeouts, and handshake retries automatically.
 *
 * ⚠ ISR Safety Rule
 *   The timer interrupt fires at a low hardware level.  Calling Serial.write()
 *   from inside an ISR risks deadlock if the Serial TX buffer is full (the TX
 *   interrupt cannot fire while we are in the timer ISR).
 *   Solution: the callback only sets a volatile flag; the actual send calls
 *   happen in loop() — outside the ISR context.
 *
 * Timer setup (AVR / ATtiny88, 8 MHz):
 *   Timer1 CTC mode, prescaler 64, OCR1A = 124 → 1 ms period.
 *   Replace with your platform timer (TimerOne, MsTimer2, etc.) as needed.
 *
 * Wiring: see IoT_Sensor_Gateway_Polling/ATtiny88_Sensor.ino
 */

#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>
#include "SharedData.h"

using namespace tinylink;

TinyArduinoAdapter adapter(Serial);
TinyLink<SensorMessage, TinyArduinoAdapter> link(adapter);

// ---------------------------------------------------------------------------
// Deferred command state — written in ISR context, consumed in loop()
// ---------------------------------------------------------------------------
static volatile bool    g_cmdPending  = false;
static volatile uint8_t g_pendingId   = 0;

// ---------------------------------------------------------------------------
// Sensor stub — replace with your real sensor library
// ---------------------------------------------------------------------------
float readTemperature() { return 21.5f; }

// ---------------------------------------------------------------------------
// Callback: fires from inside autoUpdateISR() → update()
// ⚠ We are inside a hardware ISR here — only copy data, NO Serial writes.
// ---------------------------------------------------------------------------
void onRequest(const SensorMessage& req) {
    g_pendingId  = req.requestId;
    g_cmdPending = true;
}

// ---------------------------------------------------------------------------
// Timer ISR — fires every 1 ms and drives the TinyLink engine
// ---------------------------------------------------------------------------
ISR(TIMER1_COMPA_vect) {
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR();
}

// Timer1 CTC setup: 1 ms @ 8 MHz, prescaler 64, OCR1A = 124
static void setupTimer1() {
    cli();
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    OCR1A  = 124;                                  // (8 000 000 / 64 / 1000) − 1
    TCCR1B |= (1 << WGM12);                        // CTC mode
    TCCR1B |= (1 << CS11) | (1 << CS10);           // prescaler 64
    TIMSK1 |= (1 << OCIE1A);                       // enable compare-match interrupt
    sei();
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onRequest);
    link.begin();   // Initiate TinyLink handshake.
    setupTimer1();  // Start timer — no enableAutoUpdate() needed;
                    // the constructor already registered this instance.
}

void loop() {
    // Handle the pending command outside ISR context — safe to call Serial.
    if (g_cmdPending) {
        g_cmdPending = false;           // clear first (re-entry safe)

        link.sendAck();                 // release ESP8266 from AWAITING_ACK

        SensorMessage resp;
        resp.requestId   = g_pendingId;
        resp.temperature = readTemperature();
        resp.uptime      = static_cast<uint32_t>(millis());
        link.sendData(TYPE_DATA, resp); // ATtiny88 → AWAITING_ACK
    }
}
