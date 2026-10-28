# Link Health: Diagnostics & Telemetry 📊

This example demonstrates how to monitor the physical integrity of your serial connection using the TinyStats engine. It provides a real-time report of successful transmissions versus hardware-level failures.

## 🌟 Features

**Error Tracking**: Monitors `checksumMismatches` caused by electrical noise or loose wires.
**Timeout Detection**: Detects `interByteTimeouts` when a packet starts but is never finished (e.g., a device was unplugged mid-transmission).
**Packet Counting**: Tracks total `packetsReceived` successfully verified by the **Fletcher-16** algorithm.

## 📂 Code Overview

The example uses a simple timer to print a **Health Report** every 5 seconds. This is the recommended way to debug a new **TinyLink** setup before deploying it to the field.

### Key Metrics:

**Success Packets**: Increments only when a full application packet is decoded and its checksum matches.
**Checksum/COBS Errors**: `checksumMismatches` tracks invalid packet checksums; `cobsDecodeFailures` tracks malformed COBS framing.
**Timeouts**: `interByteTimeouts` increments if a partial frame exceeds the configured `setTimeout()` window.

## 🛠 Troubleshooting with Stats

| Symptom | High metric | Likely cause |
| --- | --- | --- |
| No data | 0 `packetsReceived` | TX/RX swapped or incorrect baud rate. |
| Garbage data | High `checksumMismatches` | Bad ground connection or EMI noise. |
| Flickering link | High `interByteTimeouts` | Serial buffer overflow or loose wire. |

## 🚀 How to Run

Upload Link_Health.ino to your **MH-Tiny88** or **ESP-M3**.
Open the Serial Monitor at **9600 Baud**.
Try touching the RX/TX wires or briefly unplugging them to see the checksum and timeout counters react in real time.
