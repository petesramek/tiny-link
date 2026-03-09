# Basic Duplex: MH-Tiny88 ↔ ESP-M3 (Callback Style)

This example demonstrates a *full-duplex, bidirectional* link between an *MH-Tiny88* (Sensor Node) and an *ESP-M3* (Wi-Fi Bridge). It uses the asynchronous Callback pattern to handle data automatically, making your main loop cleaner and more event-driven.

## 🌟 Features

- *Callback Logic*: Uses link.onReceive() to trigger functions automatically upon data arrival.
- *Bi-Directional*: Both devices act as peers, sending and receiving data simultaneously.
- *Reliable*: Protected by *COBS framing* and *Fletcher-16 checksums*.

## 🛠 Hardware Setup

The MH-Tiny88 (*5V*) and ESP-M3 (*3.3V*) require a voltage divider on the Tiny88's TX line to protect the ESP.

| *MH-Tiny88 (5V)* | *Connection*           | *ESP-M3 (3.3V)*
| ---              | ---                    | ---
| TX (D1)          |	───[ 1kΩ ]──┬──	    | RX
|                  | └─[ 2kΩ ]─┤            | GND
| RX (D0)          | ──────────────         | TX
| GND              | ──────────────         | GND (Common)

## 📂 Code Overview

This example registers a listener function that executes inside the `link.update()` call:

```cpp
void myHandler(const MyData& data) {
    // Handle your data here...
    // No need to call peek() or flush()
}

void setup() {
    link.onReceive(myHandler); // Register the listener
}

void loop() {
    link.update(); // The engine executes the handler automatically
}
```

*`link.onReceive()`*: Registers a custom function to be called as soon as a valid packet is verified.
*`link.update()`*: Maintains the protocol engine and triggers the registered callback internally.
*`Asynchronous`*: The callback pattern removes the need for manual `available()` checks or `flush()` calls in your main loop.

## 🚀 How to Run
Upload `Tiny88_Sensor.ino` to the *MH-Tiny88*.
Upload `ESPM3_Bridge.ino` to the *ESP-M3*.
Observe the interaction via the Serial Monitor.
