# IoT Sensor Gateway — Mode 1: Main-Loop Polling

ATtiny88 temperature sensor node ↔ ESP8266 Wi-Fi gateway.  
This is the **simplest update mode**: `link.update()` called once per `loop()`.  
No hardware interrupts or extra libraries are required — works on every Arduino-compatible board.

---

## 🗺 Hardware Wiring

```
ATtiny88 (5 V)       Connection              ESP8266 (3.3 V)
──────────────────   ───────────────────     ──────────────────
TX (PD1)          → [1 kΩ]──┬──────────── → RX
                             └──[2 kΩ]── GND  (voltage divider)
RX (PD0)          ──────────────────────── ← TX  (3.3 V safe on Tiny88 input)
GND               ──────────────────────── GND (common)
```

---

## 📡 Protocol Flow

```
ATtiny88                              ESP8266
    │                                     │
    │◄──── HS(v=0) ─────────────────────►│  begin() on both sides
    │───── HS(v=1) ────────────────────►  │  (retries every 1 s until peer answers)
    │         both reach WAIT_FOR_SYNC    │
    │                                     │
    │◄═══ TYPE_CMD (requestId=N) ════════│  every 60 s
    │                         ESP: AWAITING_ACK
    │═══ ACK ════════════════════════════►│  sendAck() in onRequest()
    │                         ESP: WAIT_FOR_SYNC
    │═══ TYPE_DATA (temp, uptime) ═══════►│  sendData() in onRequest()
    │               ATtiny88: AWAITING_ACK│
    │◄══ ACK ════════════════════════════ │  sendAck() in onSensorData()
    │               ATtiny88: WAIT_FOR_SYNC
    │                         ESP posts to cloud
    │                                     │
    │         ... 60 seconds ...          │
```

---

## 🔑 Key Points

- **`begin()`** initiates the TinyLink handshake.  Both sides call it; whoever
  boots first retries automatically every 1 second.
- **`sendAck()`** releases the peer from `AWAITING_ACK` without waiting for the
  timeout.  Call it first in every `onDataReceived` callback.
- **`sendData()` from inside a callback** is safe and sets the sender into
  `AWAITING_ACK` correctly (the state is preserved, not overwritten).
- **`link.type()`** returns `TYPE_CMD` or `TYPE_DATA` so a single callback can
  distinguish request from response if needed.

---

## 🔄 Update Mode: Main-Loop Polling

```cpp
void loop() {
    link.update();  // ← the only required call
}
```

| Attribute             | Value                              |
|-----------------------|------------------------------------|
| Hardware interrupt?   | ❌ No                              |
| Extra library?        | ❌ No                              |
| Works if loop blocks? | ❌ No — keep `loop()` responsive   |
| Min RAM overhead      | 0 bytes                            |

> For a version that survives a blocking `loop()`, see
> **IoT_Sensor_Gateway_TimerISR**.  
> For a zero-interrupt reactive alternative, see
> **IoT_Sensor_Gateway_SerialEvent**.

---

## 🚀 How to Run

1. Edit `WIFI_SSID`, `WIFI_PASS`, and `ENDPOINT_URL` in `ESP8266_Gateway.ino`.
2. Upload `ATtiny88_Sensor.ino` to the ATtiny88 (set clock to 8 MHz).
3. Upload `ESP8266_Gateway.ino` to the ESP8266.
4. Open the ESP8266 Serial Monitor (9600 baud) to observe the handshake and the
   60-second data cycles.
