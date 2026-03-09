# TinyLink 🚀

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Framework: Arduino](https://img.shields.io)](https://www.arduino.cc)
[![TinyLink CI](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml/badge.svg)](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml)

**TinyLink** is a high-efficiency, template-based serial protocol for reliable, bidirectional UART communication. Optimized for memory-constrained devices like the **MH-Tiny88** (512B RAM) and **ESP-M3**, it provides a "pro-level" transport layer with zero-heap overhead.

---

## 🌟 Key Features

- **Strict Integrity**: Uses the **Fletcher-16** algorithm—significantly more reliable than simple sum-checks for catching bit-flips and swapped bytes.
- **COBS Framing**: Implements *Consistent Overhead Byte Stuffing*. By using `0x00` as a unique delimiter, the protocol never "desyncs" and is immune to payload data being mistaken for control characters.
- **Event-Driven API**: Support for **Asynchronous Callbacks** via `onReceive()`. Handle data the moment it arrives without cluttering your `loop()`.
- **Zero-Copy & Zero-Heap**: Optimized for 8-bit AVR. No dynamic memory allocation; data is processed directly in static buffers.
- **Multi-Platform**: Native adapters for **Arduino**, **Linux (POSIX)**, **Windows (Win32)**, **ESP-IDF**, and **STM32 HAL**.

---

## 🛠 Installation

1. Download this repository as a `.zip`.
2. In Arduino IDE, go to **Sketch > Include Library > Add .ZIP Library**.
3. For **PlatformIO**, simply drop the folder into your `lib/` directory or reference it in `platformio.ini`.

---

## 🚀 Quick Start (Callback Style)

### 1. Define your Data

Both devices must share an identical `struct`.

```cpp
struct MyData {
  uint32_t uptime;
  float temperature;
};
```

### 2. Implementation

TinyLink uses the tinylink namespace to prevent naming conflicts with other libraries.

#### 2.1 Namespace

**For Arduino/Simplified Sketches**

Add using namespace tinylink; after your includes to use the library classes directly.

```cpp
#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>

using namespace tinylink; // <--- Simplifies your code

TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);
```

** For Professional C++ / Large Projects **

It is recommended to use the explicit namespace prefix to ensure absolute symbol safety.

```cpp
#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>

tinylink::TinyPosixAdapter adapter("/dev/ttyUSB0", B9600);
tinylink::TinyLink<MyData, tinylink::TinyPosixAdapter> link(adapter);
```

#### 2.2 Usage

**Option A: Callback Style (Recommended)**

Best for clean, event-driven code. The handler triggers automatically inside `update()`.

```cpp
void onReceive(const MyData& data) {
    Serial.println(data.temperature);
}

void setup() {
    link.onReceive(onReceive);
}

void loop() {
    link.update(); // Engine handles the callback
}
```

**Option B: Polling Style**

Best for manual control or integrating into existing linear logic.

```cpp
void loop() {
    link.update(); // Keep the engine running

    if (link.available()) {
        const MyData& data = link.peek();
        Serial.println(data.temperature);
        
        link.flush(); // Clear flag for next packet
    }
}
```

### 3. Sending Data

Sending is identical for both styles:

```cpp
#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>

TinyArduinoAdapter adapter(Serial);
TinyLink<MyData, TinyArduinoAdapter> link(adapter);

void loop() {
  if(link.connected()) {
    MyData msg = { millis(), 24.5f };
    link.send(TYPE_DATA, msg);
  }
}
```

## 📊 Choosing Your Pattern

| Feature | Polling Style (available/peek) | Callback Style (onReceive)
| Logic Flow | Linear / Sequential | Event-Driven / Asynchronous
| Best For | Simple sketches, step-by-step debugging. | Complex projects, multi-tasking, clean loop().
| Control | User decides when to process data. | Data is processed the moment it arrives.
| Code Style | Traditional Arduino if checks. | Modern C++ "Listener" pattern.
| Manual Cleanup | Requires link.flush() after reading. | Automatic; no flush() or available() needed.
| RAM Usage | Identical (Zero-copy const T&). | Identical (2-byte Function Pointer).

**Which one should I use?**

**Use Polling** if your `loop()` is already very busy with `delay()` calls or if you need to wait for a specific state before "consuming" the incoming serial data.

**Use Callbacks** if you want to keep your communication logic completely separated from your application logic. This is highly recommended as it makes the `TinyLink` engine more reactive.


## 📊 Performance & Telemetry

Monitor link health in real-time to diagnose wiring issues:

```cpp
const TinyStats& stats = link.getStats();
Serial.print("Packets: "); Serial.println(stats.packets);
Serial.print("CRC Errors: "); Serial.println(stats.crcErrs);
Serial.print("Timeouts: "); Serial.println(stats.timeouts);
```

## 📂 Project Structure
`src/`: Core protocol logic (`TinyLink.h`, `TinyProtocol.h`).
`src/adapters/`: Hardware-specific drivers (Arduino, Posix, Windows, etc.).
`examples/`: Ready-to-run duplex, callback, and health-monitoring demos.
`test/`: Native C++ unit tests using `TinyTestAdapter`.

## 📜 License
This project is licensed under the MIT License.

**Developed by Pete Sramek (2026)**
