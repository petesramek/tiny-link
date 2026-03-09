# Basic Duplex: MH-Tiny88 ↔ ESP-M3

This example demonstrates a *full-duplex, bidirectional* link between an *MH-Tiny88* (acting as a sensor node) and an *ESP-M3* (acting as a Wi-Fi bridge). It utilizes *COBS framing* and *Fletcher-16 checksums* to ensure data integrity over a noisy serial line.

## 🛠 Hardware Setup

Since the MH-Tiny88 operates at *5V* and the ESP-M3 operates at *3.3V*, you must use a voltage divider or level shifter on the Tiny88's TX line to avoid damaging the ESP.

### Wiring Diagram

MH-Tiny88 (5V)	Connection	ESP-M3 (3.3V)
TX (D1)	───[ 1kΩ ]──┬──	RX
└─[ 2kΩ ]─┤ GND
RX (D0)	──────────────	TX
GND	──────────────	GND (Common)

> Note: The 2kΩ resistor goes to Ground, creating a 3.3V signal from the 5V TX.

## 📂 Example Structure

*SharedData.h*: Defines the struct MyData used by both devices. *Must be identical on both sides*.
*Tiny88_Sensor.ino*: Collects "simulated" sensor data and sends it every 2 seconds. Uses a *callback* to handle incoming commands from the ESP.
*ESPM3_Bridge.ino*: Receives sensor data and immediately sends a command response back to the Tiny88.

## 🚀 How to Run

Open Tiny88_Sensor.ino in the Arduino IDE and upload it to your *MH-Tiny88*.
Open ESPM3_Bridge.ino and upload it to your *ESP-M3*.
Open the Serial Monitor for the *ESP-M3* at *9600 Baud*.
You should see the incoming packets from the Tiny88 being displayed.

## 📈 Features Demonstrated

*Asynchronous Callbacks*: Using link.onReceive() for non-blocking command handling.
*Telemetry*: Monitoring link.getStats() for CRC errors or timeouts.
*Safety*: Automatic buffer protection via COBS delimiting.