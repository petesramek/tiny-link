# Basic Duplex: MH-Tiny88 ↔ ESP-M3 (Polling Style)

This example demonstrates a **full-duplex, bidirectional** link between an **MH-Tiny88** (Sensor Node) and an **ESP-M3** (Wi-Fi Bridge). It uses the standard **Polling** pattern to check for new data, making it easy to integrate into existing Arduino sketches.

## 🌟 Features

- **Polling Logic**: Uses `link.available()` and `link.peek()` for manual data handling.
- **Bi-Directional**: Both devices act as peers, sending and receiving data simultaneously.
- **Reliable**: Protected by **COBS** framing and **Fletcher-16 checksums**.

## 🛠 Hardware Setup

The MH-Tiny88 (**5V**) and ESP-M3 (**3.3V**) require a voltage divider on the Tiny88's TX line to protect the ESP.

| **MH-Tiny88 (5V)** | **Connection**           | **ESP-M3 (3.3V)**
| ---              | ---                        | ---
| TX (D1)          | ───[ 1kΩ ]─────┬────────── | RX
|                  |                 └─[ 2kΩ ]─┤ | GND
| RX (D0)          | ────────────────────────── | TX
| GND              | ────────────────────────── | GND (Common)

## 📂 Code Overview

This example uses a manual check inside the `loop()` function:

```cpp
void loop() {
    link.update(); // Keep the engine running

    if (link.available()) {
        const MyData& incoming = link.peek();
        // Handle your data here...
        
        link.flush(); // Clear the flag to allow the next packet
    }
}
```

- **`link.available()`**: Returns `true` only when a full, verified packet has arrived.
- **`link.peek()`**: Provides a constant reference to the data without copying it, saving RAM.
- **`link.flush()`**: Tells the library you have finished reading the data, allowing the hardware buffer to be parsed for the next packet.

## 🚀 How to Run

Upload `Tiny88_Sensor.ino` to the **MH-Tiny88**.
Upload `ESPM3_Bridge.ino` to the **ESP-M3**.
Observe the interaction via the Serial Monitor.
