# Link Health: Diagnostics & Telemetry 📊

This example demonstrates how to monitor the physical integrity of your serial connection using the TinyStats engine. It provides a real-time report of successful transmissions versus hardware-level failures.

## 🌟 Features

**Error Tracking**: Monitors `crcErrs` caused by electrical noise or loose wires.
**Timeout Detection**: Detects `timeouts` when a packet starts but is never finished (e.g., a device was unplugged mid-transmission).
**Packet Counting**: Tracks total `packets` successfully verified by the **Fletcher-16** algorithm.

## 📂 Code Overview

The example uses a simple timer to print a **Health Report** every 5 seconds. This is the recommended way to debug a new **TinyLink** setup before deploying it to the field.

### Key Metrics:

**Success Packets**: Increments only when a full **COBS** frame is decoded and the checksum matches.
**CRC/COBS Errors**: Increments if a frame is found but the data is corrupted or the COBS encoding is malformed.
**Timeouts**: Increments if the first byte of a packet is received, but no further data arrives within the `setTimeout()` window (default: 250ms).

## 🛠 Troubleshooting with Stats

**Symptom**         | **High Metric**   | **Likely Cause**
**No Data**         | 0 Packets       | TX/RX swapped or incorrect Baud Rate.
**Garbage Data**    | High `crcErrs`    | Bad Ground connection or EMI noise.
**Flickering Link** | High `timeouts`   | Serial buffer overflow or loose "jumping" wire.

## 🚀 How to Run

Upload Link_Health.ino to your **MH-Tiny88** or **ESP-M3**.
Open the Serial Monitor at **9600 Baud**.
Try touching the RX/TX wires or briefly unplugging them to see the `crcErrs` and `timeouts` counters react in real-time.
