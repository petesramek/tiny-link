# Changelog

All notable changes to TinyLink are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.5.0] – 2026-10-28

### Added
- Result-bearing adapter writes through `tinylink::WriteResult`, distinguishing complete,
  partial, failed, and zero-length requests.
- Protocol-version handshake negotiation and public connection lifecycle states.
- Sequence-correlated acknowledgements, decoded log callbacks, and structured telemetry.
- Native test suites for codecs, protocol helpers, engine behavior, and platform adapters,
  plus payload compile-contract checks.

### Changed
- Application payloads must be trivially copyable and no larger than 64 bytes. Payload object
  representations are transmitted as-is; applications with peers using different layouts must
  serialize fields explicitly.
- `begin()` reports whether the initial handshake request was fully written. A failed request
  leaves the link connecting and is retried by `update()`.
- `sendData()` and other frame-writing operations fail unless the complete encoded frame and
  delimiter are accepted by the adapter. Failed application writes do not consume a sequence
  number or enter the awaiting-ACK state.
- `update()` detects transport loss, clears partial receive and pending ACK state, and supports
  handshake recovery when a previously opened adapter becomes available again.
- Telemetry counters saturate instead of wrapping; snapshots are available through `stats()`
  and can be reset with `clearStats()`.
- Polling reads use `read()`, which returns the latest payload by value and clears its
  availability flag. Callback registration uses `onDataReceived()`.
- Application message identifiers are caller-defined `uint8_t` values. `MessageType` describes
  only TinyLink's reserved internal frames: Log (`'L'`), Ack (`'A'`), and Handshake (`'H'`).

### Removed
- The legacy `peek()` polling API; use `read()` to retrieve a payload, or use
  `onDataReceived()` for callback-driven handling.
- The legacy `TinyStats`/`getStats()` statistics API; use `Stats` and `stats()`.
- Automatic ISR update hooks. Applications must call `update()` regularly from a supported
  execution context; the engine is not an ISR API.

## [0.4.0] – 2026-01-01

### Added
- Multi-platform adapter support: Arduino, Linux (POSIX), Windows (Win32), ESP-IDF, and STM32 HAL.
- Fletcher-16 checksum, COBS framing, and a zero-heap transport design for constrained devices.
- Callback and polling receive APIs, real-time telemetry, the PlatformIO manifest, and native
  unit tests.

---

[Unreleased]: https://github.com/petesramek/tiny-link/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/petesramek/tiny-link/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/petesramek/tiny-link/releases/tag/v0.4.0
