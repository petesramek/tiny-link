# PC Monitor: Desktop to MCU 🖥️

This example demonstrates how to use **TinyLink** in a native C++ desktop environment. It allows your computer to act as a high-performance peer to your microcontrollers, enabling real-time monitoring and command injection via a standard USB-to-Serial adapter.

## 🌟 Features

**Cross-Platform**: Uses `TinyPosixAdapter` for Linux/macOS and `TinyWindowsAdapter` for Windows.
**Bi-Directional**: Receives sensor data from the MCU and sends command packets back every 5 seconds.
**Asynchronous**: Uses the `onDataReceived` callback to handle incoming COBS frames without blocking the main loop.

## 🛠 Hardware Setup

You will need a **USB-to-TTL Serial Adapter** (e.g., CP2102, FT232RL, or CH340).

PC (via USB Adapter)	Connection	MCU (MH-Tiny88 / ESP)
TX	──────────────	RX
RX	──────────────	TX
GND	──────────────	GND (Common)

> ⚠️ Warning: If connecting to an **ESP-M3**, ensure your USB adapter is set to **3.3V mode**. If connecting to an **MH-Tiny88**, ensure it is in **5V mode**.

## 🚀 How to Build & Run

1. Identify your Port

**Windows**: Check Device Manager (e.g., `COM3`).
**Linux/macOS**: Run ls `/dev/tty**` (e.g., `/dev/ttyUSB0` or √/dev/cu.usbserial-xxx√).

2. Compile

Open a terminal in this directory and use any standard C++ compiler:

### Linux / macOS:

```bash
bash
g++ main.cpp -I../../src -o tiny_monitor
./tiny_monitor
```

### Windows (MinGW/MSYS2):

```bash
bash
g++ main.cpp -I../../src -o tiny_monitor.exe
./tiny_monitor.exe
```

## 📂 Code Overview

**SharedData.h**: Defines the struct MyData used by both devices. **Must be identical on both sides**.
**PCAdapter**: A type-alias that automatically selects the correct OS-specific driver.
**onMcuData**: The callback function triggered whenever a valid Fletcher-16 verified packet arrives.
