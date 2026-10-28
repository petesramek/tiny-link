# IoT sensor gateway (loop-driven)

The sketches in this folder demonstrate the ATtiny88-to-ESP8266 sensor gateway using the current
TinyLink API. `TinyLink` does not provide automatic timer-ISR updates; both devices call
`link.update()` from `loop()`.

The ATtiny88-side sketch retains its historical timer setup, but does not process TinyLink frames
from the interrupt handler. Do not move `link.update()` into an ISR: it reads the transport,
updates timers, and may invoke application callbacks.

The sender waits for an ACK before sending another application frame. Call `sendAck()` from the
receiver after accepting each payload. Both peers must call `begin()` to negotiate the connection.

For the simplest loop-driven version without the legacy timer setup, see
[`IoT_Sensor_Gateway_Polling`](../IoT_Sensor_Gateway_Polling/README.md).
