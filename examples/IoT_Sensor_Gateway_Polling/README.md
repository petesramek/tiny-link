# IoT sensor gateway (loop-driven)

This example demonstrates a bidirectional sensor request/response link between an ATtiny88 and
an ESP8266. Both peers call `begin()` to negotiate the connection, and both must call
`link.update()` regularly from `loop()`.

The application uses its own `TYPE_CMD` and `TYPE_DATA` byte values. `MessageType` contains only
TinyLink's reserved internal frames and does not define application command or data types. The
receive callback receives the payload only; it does not expose the application message type.
Use the peer's role to interpret expected payloads, or include a discriminator in the payload if
one receiver must handle multiple kinds.

The receiver should acknowledge an accepted application frame with `sendAck()`. A sender can
have only one application frame awaiting acknowledgement at a time; check `canSend()` before
sending another.

Payload structures are copied as raw object bytes and are not a portable wire format by
themselves. Both boards must agree on field sizes, byte order, and layout, or the application
must encode and decode fields explicitly.
