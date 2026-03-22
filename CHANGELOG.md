# Changelog

All notable changes to **TinyLink** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- ─────────────────────────────────────────────────────────────────────────
     HOW TO ADD ENTRIES
     ─────────────────────────────────────────────────────────────────────────
     For every release, copy the [Unreleased] section below, set the version
     number and date, then clear the [Unreleased] section for the next cycle.

     Subsections (use only those that apply):
       ### Added      – new features
       ### Changed    – changes in existing functionality
       ### Deprecated – features that will be removed in a future release
       ### Removed    – features removed in this release
       ### Fixed      – bug fixes
       ### Security   – security-related fixes or improvements
     ──────────────────────────────────────────────────────────────────────── -->

## [Unreleased]

### Added
- `src/protocol/Status.h`: focused header with expanded `TinyStatus` enum (STATUS_OK, ERR_CRC,
  ERR_TIMEOUT, ERR_OVERFLOW, ERR_BUSY, ERR_PROCESSING, ERR_UNKNOWN) and `tinystatus_to_string()` helper.
- `src/protocol/State.h`: focused header with expanded `TinyState` enum (CONNECTING, HANDSHAKING,
  WAIT_FOR_SYNC, IN_FRAME, FRAME_COMPLETE, AWAITING_ACK).
- `src/protocol/Stats.h`: focused header with `TinyStats` struct (extracted from `TinyProtocol.h`),
  plus a new `clear()` method.
- `src/protocol/AckMessage.h`: packed `TinyAck` struct (`uint8_t seq` + `TinyStatus result`,
  2 bytes) for `MessageType::Ack` (`'A'`) frames.
- `src/protocol/DebugMessage.h`: packed `DebugMessage` struct (32 bytes: `uint32_t ts`,
  `uint8_t level`, `char text[27]`) for `MessageType::Debug` (`'g'`) frames, with
  `debugmessage_set_text()` helper and `DEBUG_TEXT_CAPACITY` constant.
- `MessageType::Ack = 'A'` added to `src/protocol/MessageType.h`.
- `src/protocol/MessageType.h`: focused header with `MessageType` enum, `message_type_from_wire()`,
  `message_type_to_wire()`, and `message_type_to_string()` helpers.
- Migration guide in README for all breaking changes in this release.
- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, issue/PR templates,
  and `CITATION.cff` for OSS best practices.
- Unity-style unit tests: `test_stats`, `test_ackmessage`, `test_ackmessage_more`,
  `test_debugmessage`, `test_debugmessage_edgecases`, `test_message_type`, `test_status_strings`.
  All registered in `test/test_runner.cpp`.

### Changed
- `src/TinyProtocol.h` is now a lightweight umbrella header that re-exports all focused
  `src/protocol/*.h` headers. Existing `#include "TinyProtocol.h"` continues to work unchanged.
- `TinyStatus` expanded with granular ACK codes (ERR_OVERFLOW, ERR_BUSY, ERR_PROCESSING,
  ERR_UNKNOWN) and moved to `src/protocol/Status.h`.  Wire values for ERR_CRC (0x01) and
  ERR_TIMEOUT (0x02) are now explicitly assigned.
- `TinyState` expanded with CONNECTING, HANDSHAKING, FRAME_COMPLETE, AWAITING_ACK states and
  moved to `src/protocol/State.h`.
- `TinyStats` moved to `src/protocol/Stats.h`; gained `clear()` method.
- Examples updated to use `message_type_to_wire(MessageType::Cmd)`.
- Payload size limit reduced from 240 bytes to 64 bytes to enforce micro-message design intent
  and eliminate a latent `_rawIdx` overflow bug on large payloads.

### Removed
- **[Breaking]** `MessageType::Req` (wire byte `'R'`) removed; replaced by `MessageType::Cmd`
  (wire byte `'C'`).  The legacy `'R'` mapping in `message_type_from_wire()` has been dropped —
  peers must use `'C'` for command frames going forward.
- **[Breaking]** `TinyResult` enum removed; replaced by the unified `TinyStatus` enum in
  `src/protocol/Status.h`.  `Result.h` has been superseded by `AckMessage.h`.

---

## [0.4.0] – 2026-01-01

### Added
- Multi-platform adapter support: Arduino, Linux (POSIX), Windows (Win32), ESP-IDF, and STM32 HAL.
- Fletcher-16 checksum for robust error detection.
- COBS framing for reliable stream synchronisation.
- Zero-heap, zero-copy design suitable for 8-bit AVR (≥ 512 B RAM).
- Event-driven callback API (`onReceive()`).
- Polling API (`available()` / `peek()` / `flush()`).
- Real-time telemetry via `getStats()` (packets, CRC errors, timeouts).
- PlatformIO library manifest (`library.properties`, `platformio.ini`).
- Native unit test suite using `TinyTestAdapter`.
- Comprehensive `README.md` with usage examples, architecture notes, and hardware wiring guide.

---

[Unreleased]: https://github.com/petesramek/tiny-link/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/petesramek/tiny-link/releases/tag/v0.4.0
