# TinyLink 🚀

[![TinyLink CI](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml/badge.svg)](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

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

Both devices must share an identical `struct` with identical size that is ensured by `__attribute__((packed))`.

```cpp
struct MyData {
  uint32_t uptime;
  float temperature;
} __attribute__((packed));
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

**For Professional C++ / Large Projects**

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

| Feature | Polling Style (available/peek) | Callback Style (onReceive) |
| --- |  --- |  --- | 
| Logic Flow | Linear / Sequential | Event-Driven / Asynchronous |
| Best For | Simple sketches, step-by-step debugging. | Complex projects, multi-tasking, clean loop(). |
| Control | User decides when to process data. | Data is processed the moment it arrives. |
| Code Style | Traditional Arduino if checks. | Modern C++ "Listener" pattern. |
| Manual Cleanup | Requires link.flush() after reading. | Automatic; no flush() or available() needed. |
| RAM Usage | Identical (Zero-copy const T&). | Identical (2-byte Function Pointer). |

**Which one should I use?**

**Use Polling** if your `loop()` is already very busy with `delay()` calls or if you need to wait for a specific state before "consuming" the incoming serial data.

**Use Callbacks** if you want to keep your communication logic completely separated from your application logic. This is highly recommended as it makes the `TinyLink` engine more reactive.


## 🔄 Auto-Update Modes

TinyLink's protocol engine must be called periodically to process incoming bytes,
manage timeouts, and fire callbacks.  There are **three supported ways** to drive
it — choose the one that fits your hardware and application:

| | Mode 1 — Main Loop | Mode 2 — Timer ISR | Mode 3 — serialEvent() |
|---|---|---|---|
| **How** | `link.update()` in `loop()` | `autoUpdateISR()` on a hardware timer | `autoUpdateISR()` from `serialEvent()` |
| **Hardware interrupt?** | ❌ No | ✅ Timer required | ❌ No |
| **Extra setup call?** | None | Timer init + `attachInterrupt` | None |
| **`enableAutoUpdate()`?** | Not needed | Not needed | Not needed |
| **Works if loop() blocks?** | ❌ No | ✅ Yes | ❌ No |
| **Best for** | Any board, simplest code | Deterministic, busy loops | No-timer boards, reactive RX |

### Mode 1 — Main Loop (default, works everywhere)

```cpp
void loop() {
    link.update();  // ← only required call; works on every board
}
```

### Mode 2 — Timer ISR (deterministic, interrupt-driven)

The constructor auto-registers the instance — no `enableAutoUpdate()` needed.

```cpp
#include <TimerOne.h>

void setup() {
    Timer1.initialize(1000);  // 1 ms
    Timer1.attachInterrupt(TinyLink<MyData, TinyArduinoAdapter>::autoUpdateISR);
    // No enableAutoUpdate() call required.
}
// loop() does not need to call update() at all.
```

### Mode 3 — serialEvent() (zero-setup, no timer needed)

Arduino calls `serialEvent()` automatically between `loop()` iterations when
Serial bytes are waiting — no library or interrupt configuration required.

```cpp
void serialEvent() {
    TinyLink<MyData, TinyArduinoAdapter>::autoUpdateISR();
}
// setup() and loop() have no TinyLink maintenance calls.
```

> **Multi-instance note:** When you have two `TinyLink<T,Adapter>` objects of
> the same type, the last-constructed one is the default ISR target.  Call
> `link.enableAutoUpdate()` on the desired instance to override.

For a complete two-device IoT scenario in all three modes, see the
[`IoT_Sensor_Gateway_*`](examples/) family of examples below.

---

## 🌐 IoT Sensor Gateway — Full Bidirectional Examples

The `IoT_Sensor_Gateway_*` examples demonstrate a realistic ATtiny88 ↔ ESP8266
system: the ATtiny88 reads a temperature sensor, the ESP8266 requests readings
every 60 seconds and forwards them to a cloud endpoint.

### Scenario

```
ATtiny88                              ESP8266
    │◄──── Handshake ───────────────────►│  begin() on both sides
    │             both: WAIT_FOR_SYNC    │
    │                                    │
    │◄═══ TYPE_CMD (request) ════════════│  every 60 s
    │═══ ACK ════════════════════════════►│  sendAck() in callback
    │═══ TYPE_DATA (temp + uptime) ═════►│
    │◄══ ACK ════════════════════════════ │  sendAck() in callback
    │                         ESP: posts to cloud
```

### `sendAck()` — Releasing the Peer from AWAITING_ACK

When `begin()` is called (handshake mode), every `sendData()` puts the sender
into `AWAITING_ACK`.  The receiver must call `sendAck()` to release it promptly:

```cpp
void onReceive(const SensorMessage& data) {
    link.sendAck();          // ← release peer from AWAITING_ACK immediately
    link.sendData(TYPE_DATA, response);  // reply — safe to call from callback
}
```

### Three Update Modes, Side by Side

| Example folder                     | update() driven by   | ISR-safe callbacks? | HW interrupt? |
|------------------------------------|----------------------|---------------------|---------------|
| `IoT_Sensor_Gateway_Polling`       | `loop()`             | ✅ Yes              | ❌ No         |
| `IoT_Sensor_Gateway_TimerISR`      | Hardware timer ISR   | ⚠ Deferred only    | ✅ Yes        |
| `IoT_Sensor_Gateway_SerialEvent`   | `serialEvent()`      | ✅ Yes              | ❌ No         |

> **Mode 2 (Timer ISR) rule:** callbacks fire inside a hardware ISR — only set
> `volatile` flags there.  Call `sendAck()` and `sendData()` from `loop()`.


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

`src/protocol/`: Focused protocol headers (`MessageType.h`, `Status.h`, `State.h`, `Stats.h`, `AckMessage.h`, `DebugMessage.h`).

`src/adapters/`: Hardware-specific drivers (Arduino, Posix, Windows, etc.).

`examples/`: Ready-to-run duplex, callback, and health-monitoring demos.

`test/`: Native C++ unit tests using `TinyTestAdapter`.

## 🔄 Migration Guide

### `MessageType::Req` → `MessageType::Cmd` (wire `'R'` → `'C'`) — **Breaking Change**

`MessageType::Req` (wire byte `'R'`) has been removed and replaced with `MessageType::Cmd`
(wire byte `'C'`). Legacy `'R'` bytes are **no longer accepted** by the parser.

**Action required**: Update all peers to send `'C'` for command frames.

```cpp
// Outgoing command frames:
link.send(message_type_to_wire(MessageType::Cmd), msg);   // emits 'C'

// Parsing — only 'C' is accepted:
MessageType mt;
if (message_type_from_wire(link.type(), mt) && mt == MessageType::Cmd) {
    // handle command
}
```

### `MessageType::Ack` added (wire `'A'`)

ACK/NACK frames now use `MessageType::Ack` (`'A'`) carrying a `TinyAck` payload:

```cpp
// TinyAck payload: 2 bytes — seq (uint8_t) + result (TinyStatus)
tinylink::TinyAck ack;
ack.seq    = link.seq();
ack.result = tinylink::TinyStatus::STATUS_OK;
link.send(message_type_to_wire(tinylink::MessageType::Ack), ack);
```

### `TinyStatus` — consolidated ACK/error codes

`TinyStatus` now carries granular ACK codes (previously spread across a separate
`TinyResult` type, which has been removed):

| Value | Code | Meaning |
|-------|------|---------|
| `0x00` | `STATUS_OK` | No error; operation succeeded |
| `0x01` | `ERR_CRC` | Checksum or framing failure |
| `0x02` | `ERR_TIMEOUT` | Inter-byte timeout |
| `0x03` | `ERR_OVERFLOW` | Receive buffer overflow |
| `0x04` | `ERR_BUSY` | Peer is busy; retry later |
| `0x05` | `ERR_PROCESSING` | Peer is still processing a prior command |
| `0xFF` | `ERR_UNKNOWN` | Unspecified error |

Use `tinystatus_to_string()` for human-readable diagnostics:

```cpp
#include "protocol/Status.h"
const char* msg = tinylink::tinystatus_to_string(tinylink::TinyStatus::ERR_CRC);
```

### `DebugMessage` — structured debug payload (`MessageType::Debug` / `'g'`)

```cpp
#include "protocol/DebugMessage.h"
tinylink::DebugMessage dbg;
dbg.ts    = millis();
dbg.level = 1;
tinylink::debugmessage_set_text(dbg, "boot complete");
link.send(message_type_to_wire(tinylink::MessageType::Debug), dbg);
```

## 📜 License
This project is licensed under the MIT License.

**Developed by Pete Sramek (2026)**
