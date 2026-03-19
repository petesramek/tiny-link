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
- `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`, `CHANGELOG.md`, issue/PR templates, and `CITATION.cff` for OSS best practices.

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
