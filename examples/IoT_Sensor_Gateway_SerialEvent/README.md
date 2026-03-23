# IoT Sensor Gateway — Mode 3: serialEvent()

ATtiny88 temperature sensor node ↔ ESP8266 Wi-Fi gateway.  
The TinyLink engine is driven by Arduino's built-in **`serialEvent()`** — no
hardware timer, no extra library, no `link.update()` in `loop()`.

---

## 🗺 Hardware Wiring

Same as Mode 1 (Polling).  See `IoT_Sensor_Gateway_Polling/README.md`.

---

## 📡 Protocol Flow

Identical to Mode 1 and Mode 2.

```
ATtiny88                              ESP8266
    │   serialEvent() → update()         │   serialEvent() → update()
    │◄══ TYPE_CMD ═══════════════════════│  every 60 s (from loop)
    │   callback fires in serialEvent(): │
    │     sendAck()                      │
    │     sendData(TYPE_DATA, resp) ════►│
    │                                    │   callback fires in serialEvent():
    │◄══ ACK ════════════════════════════│     sendAck()
    │                                    │     postToCloud(...)
    │         ... 60 seconds ...         │
```

---

## 🔄 Update Mode: serialEvent()

Arduino calls `serialEvent()` automatically between `loop()` iterations
whenever bytes are waiting in the hardware UART RX buffer.

```cpp
void serialEvent() {
    TinyLink<SensorMessage, TinyArduinoAdapter>::autoUpdateISR();
}
```

Because `serialEvent()` runs between `loop()` calls (not inside a hardware
ISR), callbacks are free to call `sendAck()`, `sendData()`, and any other
Arduino API directly — **no deferred flag pattern needed** (contrast with
Mode 2).

| Attribute                  | Value                                          |
|----------------------------|------------------------------------------------|
| Hardware interrupt?        | ❌ No                                          |
| Extra library?             | ❌ No                                          |
| Works if loop() blocks?    | ❌ No — not called during delay/HTTP POST      |
| ISR callback restriction?  | ✅ None — Serial, WiFi, etc. are all safe      |
| Min RAM overhead           | 0 bytes                                        |

### When does serialEvent() NOT fire?

`serialEvent()` is skipped while `loop()` is blocked inside:
- `delay()`
- `WiFi.begin()` / `while (!WL_CONNECTED)`
- A slow HTTP POST

For the 60-second IoT use case this is acceptable: the HTTP POST completes
quickly, and the ATtiny88 will retry its handshake frames until the ESP8266
can answer.  For endpoints with multi-second response times, use **Mode 2
(TimerISR)** instead.

---

## ✅ Mode Comparison Summary

| Mode | File folder                        | update() driven by   | ISR-safe callbacks? |
|------|------------------------------------|----------------------|---------------------|
| 1    | `IoT_Sensor_Gateway_Polling`       | `loop()`             | ✅ Yes              |
| 2    | `IoT_Sensor_Gateway_TimerISR`      | Hardware timer ISR   | ⚠ Deferred only    |
| 3    | `IoT_Sensor_Gateway_SerialEvent`   | `serialEvent()`      | ✅ Yes              |

---

## 🚀 How to Run

1. Edit `WIFI_SSID`, `WIFI_PASS`, and `ENDPOINT_URL` in `ESP8266_Gateway.ino`.
2. Upload `ATtiny88_Sensor.ino` to the ATtiny88 (clock fuse: 8 MHz internal).
3. Upload `ESP8266_Gateway.ino` to the ESP8266.
4. Open the Serial Monitor on the ESP8266 (9600 baud).  After the handshake
   completes you will see a cloud POST every 60 seconds.
