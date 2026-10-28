# TinyLink 🚀

[![TinyLink CI](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml/badge.svg)](https://github.com/petesramek/tiny-link/actions/workflows/cpp-ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> Disclaimer: It is a vibe-coded project primarily used to learn how software for AVR is developed. At the same time its purpose is to understand how GitHub Copilot works and if there is any benefit using it. I discourage using the project in any scenario other than testing and validating your skills while developing DYI projects.

**TinyLink** is a high-efficiency, template-based serial protocol for reliable, bidirectional UART communication. Optimized for memory-constrained devices like the **MH-Tiny88** (512B RAM) and **ESP-M3**, it provides a "pro-level" transport layer with zero-heap overhead.

---

## 🌟 Key Features

- **Strict Integrity**: Uses the **Fletcher-16** algorithm—significantly more reliable than simple sum-checks for catching bit-flips and swapped bytes.

- **COBS Framing**: Implements *Consistent Overhead Byte Stuffing*. By using `0x00` as a unique delimiter, the protocol never "desyncs" and is immune to payload data being mistaken for control characters.

- **Event-Driven API**: Register callbacks with `onDataReceived()`; callbacks run while `update()` processes incoming frames.

- **Allocation-Free**: Optimized for 8-bit AVR. No dynamic memory allocation; frames are processed in fixed-size buffers.

- **Multi-Platform**: Native adapters for **Arduino**, **Linux (POSIX)**, **Windows (Win32)**, **ESP-IDF**, and **STM32 HAL**.

---

## 🛠 Installation

1. Download this repository as a `.zip`.
2. In Arduino IDE, go to **Sketch > Include Library > Add .ZIP Library**.
3. For **PlatformIO**, simply drop the folder into your `lib/` directory or reference it in `platformio.ini`.

---

## 🚀 Quick Start

### 1. Define a payload and application message type

Application payloads are copied as opaque object bytes. Their types must be trivially copyable
and no larger than 64 bytes. Both peers must agree on the payload representation; padding,
endianness, alignment, and ABI can differ across platforms. For portable communication between
different platforms, serialize fields explicitly into a defined wire format.

Use an application-defined message byte (for example, `'D'` for sensor data). `MessageType`
contains only TinyLink's reserved internal frame types (`Log`, `Ack`, and `Handshake`); it is not
an enum of application message types.

```cpp
#include <stdint.h>
#include <TinyLink.h>
#include <adapters/TinyArduinoAdapter.h>

struct SensorData {
  uint32_t uptime;
  int16_t temperatureCentiC;
};

static const uint8_t TYPE_SENSOR_DATA = 'D';

tinylink::TinyArduinoAdapter adapter(Serial);
tinylink::TinyLink<SensorData, tinylink::TinyArduinoAdapter> link(adapter);

void onSensorData(const SensorData& data) {
  Serial.println(data.temperatureCentiC);
  link.sendAck();
}

void setup() {
  Serial.begin(9600);
  link.onDataReceived(onSensorData);
  link.begin();
}

void loop() {
  link.update();
}

void sendCurrentReading() {
  if (!link.connected() || !link.canSend()) {
    return;
  }

  const SensorData data = { static_cast<uint32_t>(millis()), 2450 };
  link.sendData(TYPE_SENSOR_DATA, data);
}
```

Call `begin()` on both peers to negotiate a connection. `update()` must be called regularly
from the application loop to process received bytes, timeouts, and callbacks. `sendData()`
requires a connected link and an available transmit slot; it returns `false` if the complete
frame was not written. Call it when the application has data to send, and check its return
value. The receiver should call `sendAck()` after accepting a frame to release the sender's
stop-and-wait transmit slot.

## 📊 Choosing a receive style

Use `onDataReceived()` for callback-driven handling, or use `available()` and `read()` to poll
for one received payload. `read()` returns the payload by value and clears the availability
flag; `flush()` can clear the flag without reading the payload. In either style, keep calling
`update()` regularly.


## 📊 Performance & Telemetry

Monitor link health in real-time to diagnose wiring issues:

```cpp
const tinylink::Stats stats = link.stats();
Serial.print("Packets: "); Serial.println(stats.packetsReceived);
Serial.print("Checksum Errors: "); Serial.println(stats.checksumMismatches);
Serial.print("ACK Timeouts: "); Serial.println(stats.ackTimeouts);
```

## Adapter Write Results

Built-in adapter `write()` overloads return `tinylink::WriteResult`, with a
`WriteStatus` and the number of bytes accepted by the platform. The status is
`Complete`, `Partial`, `Failed`, or `ZeroLength`. Adapters do not retry partial
writes. Applications implementing a custom adapter must return this result
from both the single-byte and buffer `write()` overloads, and must not treat a
partial frame as complete.

## Connection lifecycle and transport loss

`update()` detects a closed adapter and moves the link to
`ConnectionState::Failed`. The link clears any partial receive frame, pending
outgoing ACK expectation, and received-sequence ACK eligibility. It does not
retransmit an application frame that was already sent or reset the next
transmit sequence. An application payload already delivered to polling code
remains available to `read()` or `flush()`.

When the adapter is open again, a link started with `begin()` returns to
`ConnectionState::Connecting` and sends a fresh handshake request; the existing
handshake retry interval applies if the request cannot be written or the peer
does not respond. A link that was synchronized by a peer handshake without
calling `begin()` returns to `ConnectionState::Inactive` and waits for a new
peer handshake request (or an application call to `begin()`). Transport loss
is recoverable; monitor `connectionState()` or `connected()` to observe it.

## 📂 Project Structure

`src/TinyLink.h`: Core protocol engine and public API.

`src/protocol/`: Public connection states, message types, ACK results, and telemetry.

`src/adapters/`: Hardware-specific transport adapters (Arduino, POSIX, Windows, ESP-IDF, and STM32 HAL).

`src/internal/`: Packet codecs and wire-protocol implementation details.

`examples/`: Hardware and transport demonstrations; see each example README for prerequisites
and supported update patterns.

`test/`: Native C++ unit tests using Unity. Run with `pio test -e native` (built with `-Wall -Wextra -Werror -pedantic`); `pio test -e native_sanitized` adds ASan/UBSan (needs sanitizer runtimes; run in CI).

## 🔄 Protocol and compatibility notes

Application message identifiers are `uint8_t` values supplied to `sendData()`. The reserved
internal identifiers are `MessageType::Log` (`'L'`), `MessageType::Ack` (`'A'`), and
`MessageType::Handshake` (`'H'`); do not use those values for application payloads.

`begin()` starts protocol-version negotiation. Its `bool` result reports whether the initial
handshake request was fully written; if it returns `false`, the link remains connecting and
retries from `update()`. Application sends use `sendData()` and the receiver acknowledges an
accepted frame with `sendAck()`. A link supports one outstanding acknowledged application
send at a time; use `canSend()` to check readiness.

Payload types must be trivially copyable and at most 64 bytes. TinyLink transmits object bytes
as-is; for peers with differing representations, define an explicit field encoding instead.
See the [changelog](CHANGELOG.md) for the complete 0.5.0 changes and migration details.

## Testing

Run the native unit suite and the dedicated payload compile-contract checks:

```sh
pio test -e native
python tools/compile_contract/check.py
```

The compile-contract check uses a native GNU or Clang C++11 compiler. It verifies that a valid
payload, including a 64-byte payload at TinyLink's maximum, compiles, and that a payload with a
non-trivial destructor fails specifically at TinyLink's trivially-copyable assertion.

## 📜 License
This project is licensed under the MIT License.

**Developed by Pete Sramek (2026)**
