# Security Policy

## Supported Versions

The following versions of TinyLink currently receive security fixes:

| Version | Supported          |
| ------- | ------------------ |
| 0.4.x   | ✅ Yes             |
| < 0.4   | ❌ No              |

## Reporting a Vulnerability

**Please do not open a public GitHub issue for security vulnerabilities.**

If you discover a security vulnerability in TinyLink, please follow responsible
disclosure guidelines and report it privately so that it can be addressed before
public disclosure.

### How to Report

1. **Contact the maintainer directly** via the email address listed on
   [Pete Sramek's GitHub profile](https://github.com/petesramek), or by opening
   a [GitHub Security Advisory](https://github.com/petesramek/tiny-link/security/advisories/new)
   (available under the **Security** tab of this repository).

2. **Include the following information** in your report:
   - A clear description of the vulnerability and its potential impact.
   - Steps to reproduce the issue (proof-of-concept code is helpful).
   - The version(s) of TinyLink affected.
   - Any suggested mitigations or fixes, if you have them.

### What to Expect

- **Acknowledgement**: You will receive an acknowledgement within **72 hours**
  of your report.
- **Assessment**: The maintainer will assess severity and scope. You will be
  kept informed of progress.
- **Fix & Disclosure**: A fix will be developed in a private branch and released
  as soon as practical. Once a patched release is available, a public disclosure
  (GitHub Security Advisory) will be published. Credit will be given to the
  reporter unless anonymity is requested.
- **Timeline**: We aim to resolve critical vulnerabilities within **14 days**
  and moderate/low severity issues within **90 days**.

## Scope

TinyLink is a C++ header-only library for embedded/microcontroller serial
communication. Security considerations relevant to this library include:

- **Packet integrity**: Bypass or weakening of Fletcher-16 checksum validation.
- **Buffer overflows**: Any input that causes out-of-bounds reads or writes in
  the COBS framing or packet parsing logic.
- **Protocol desync attacks**: Crafted byte sequences that could permanently
  stall or confuse the state machine.
- **Adapter-specific issues**: Vulnerabilities specific to any of the platform
  adapters (Arduino, POSIX, Windows, ESP-IDF, STM32 HAL).

Issues that are clearly out of scope (e.g., host OS vulnerabilities, physical
hardware attacks) will be politely declined.

## Preferred Languages

Reports may be submitted in **English**.

Thank you for helping keep TinyLink and its users safe!
