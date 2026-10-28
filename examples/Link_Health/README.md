# Link health

This example periodically prints TinyLink telemetry from `link.stats()`. The returned
`tinylink::Stats` snapshot includes successful packets, checksum and COBS decode failures,
inter-byte timeouts, and ACK timeouts, among other counters. Counters saturate instead of
wrapping; call `link.clearStats()` to reset them.

The sketch calls `link.update()` from `loop()`. Use the error and timeout counters to help
separate framing/integrity problems from peers that are not acknowledging transmissions.
