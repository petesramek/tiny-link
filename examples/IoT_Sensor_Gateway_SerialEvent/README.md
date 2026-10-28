# IoT sensor gateway (serialEvent-driven)

These sketches use the current `link.update()` instance API from Arduino's `serialEvent()`;
TinyLink has no static `autoUpdateISR()` hook. On boards that invoke `serialEvent()` between
`loop()` iterations, it can process waiting serial input without a hardware timer.

`serialEvent()` is cooperative, not interrupt-driven. It does not run while `loop()` is blocked,
so keep loop work short. Call `update()` on each TinyLink instance that shares the serial
transport.

Both peers call `begin()` to negotiate a connection. After receiving an application payload, the
receiver should call `sendAck()` so the sender can use its next transmit slot. For the basic
loop-driven version, see
[`IoT_Sensor_Gateway_Polling`](../IoT_Sensor_Gateway_Polling/README.md).
