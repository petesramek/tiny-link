# IoT Sensor Gateway — Mode 2: Hardware Timer ISR

ATtiny88 temperature sensor node ↔ ESP8266 Wi-Fi gateway.  
The TinyLink engine is driven by a **hardware timer interrupt** on each device.
`loop()` contains no `link.update()` call — the ISR handles all RX processing,
timeout management, and handshake retries automatically.

---

## 🗺 Hardware Wiring

Same as Mode 1 (Polling).  See `IoT_Sensor_Gateway_Polling/README.md`.

---

## 📡 Protocol Flow

Identical to Mode 1.  The only difference is **who calls `update()`**:

```
ATtiny88                              ESP8266
    │   Timer1 ISR → autoUpdateISR()     │   Ticker → autoUpdateISR()
    │◄══ TYPE_CMD ═══════════════════════│  every 60 s (from loop)
    │   callback sets g_cmdPending flag  │
    │   loop() detects flag, calls:      │
    │     sendAck()                      │
    │     sendData(TYPE_DATA, resp) ════►│  data arrives; callback sets flag
    │                                    │  loop() detects flag, calls:
    │◄══ ACK ════════════════════════════│    sendAck()
    │                                    │    postToCloud(...)
```

---

## ⚠ ISR Safety: Why Deferred Sends?

On AVR (ATtiny88) and ESP8266, calling `Serial.write()` from a hardware timer
ISR can deadlock if the TX buffer is full — the UART TX interrupt cannot fire
while the higher-priority timer ISR is running.

**Rule of thumb for Mode 2:**
- Inside `onDataReceived()`: **only copy data and set volatile flags.**
- In `loop()`: call `sendAck()`, `sendData()`, and any network operations.

```cpp
// ✅ Safe — ISR callback only sets a flag
void onRequest(const SensorMessage& req) {
    g_pendingId  = req.requestId;
    g_cmdPending = true;
}

// ✅ Safe — Serial writes happen in loop()
void loop() {
    if (g_cmdPending) {
        g_cmdPending = false;
        link.sendAck();
        link.sendData(TYPE_DATA, response);
    }
}
```

---

## 🔄 Update Mode: Hardware Timer ISR

| Device   | Timer mechanism                              |
|----------|----------------------------------------------|
| ATtiny88 | AVR Timer1 CTC, 1 ms @ 8 MHz (registers)    |
| ESP8266  | `Ticker` (built-in Arduino core), 1 ms       |

```cpp
// ATtiny88 — ISR vector calls autoUpdateISR()
ISR(TIMER1_COMPA_vect) {
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR();
}

// ESP8266 — Ticker calls autoUpdateISR() every 1 ms
tinyLinkTicker.attach_ms(1,
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR);
```

No `enableAutoUpdate()` call needed — the constructor auto-registers the
instance.

| Attribute                   | Value                                    |
|-----------------------------|------------------------------------------|
| Hardware interrupt?         | ✅ Yes — timer                          |
| Extra library (ATtiny88)?   | ❌ No — raw AVR registers               |
| Extra library (ESP8266)?    | ❌ No — `Ticker` is in the Arduino core |
| Works if loop() blocks?     | ✅ Yes                                  |
| ISR callback restriction    | ⚠ No Serial writes — use deferred flag |

> For a simpler version without hardware timers, see  
> **IoT_Sensor_Gateway_Polling** or **IoT_Sensor_Gateway_SerialEvent**.

---

## 🚀 How to Run

1. Edit `WIFI_SSID`, `WIFI_PASS`, and `ENDPOINT_URL` in `ESP8266_Gateway.ino`.
2. Upload `ATtiny88_Sensor.ino` to the ATtiny88 (clock fuse: 8 MHz internal).
3. Upload `ESP8266_Gateway.ino` to the ESP8266.
4. Open the ESP8266 Serial Monitor (9600 baud) — you will see 60-second cycles
   once both devices complete the handshake.
