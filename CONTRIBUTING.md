# Contributing to TinyLink

Thank you for your interest in contributing to **TinyLink**! Whether you are reporting a bug, proposing a new feature, improving documentation, or submitting code, your help is greatly appreciated.

---

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [How to Contribute](#how-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Requesting Features](#requesting-features)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Development Setup](#development-setup)
- [Running Tests](#running-tests)
- [Code Style](#code-style)
- [Commit Messages](#commit-messages)

---

## Code of Conduct

This project adheres to the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating, you are expected to uphold this standard. Please report unacceptable behavior to the maintainer at the contact listed in `CODE_OF_CONDUCT.md`.

---

## Getting Started

1. **Fork** the repository on GitHub.
2. **Clone** your fork locally:
   ```bash
   git clone https://github.com/<your-username>/tiny-link.git
   cd tiny-link
   ```
3. **Create a branch** for your change:
   ```bash
   git checkout -b feat/my-new-feature
   ```

---

## How to Contribute

### Reporting Bugs

Use the [Bug Report](.github/ISSUE_TEMPLATE/bug_report.md) issue template. Provide:
- TinyLink version (see `src/version.h`)
- Target platform and compiler/toolchain version
- A minimal, self-contained code example that reproduces the issue
- Expected vs. actual behaviour

### Requesting Features

Use the [Feature Request](.github/ISSUE_TEMPLATE/feature_request.md) issue template. Describe the problem you are trying to solve and your proposed solution.

### Submitting Pull Requests

1. **Open an issue first** for non-trivial changes so we can agree on the approach.
2. Keep PRs **focused**: one logical change per PR.
3. Update or add documentation if your change affects the public API or behaviour.
4. Ensure all existing tests pass and add new tests for new functionality.
5. Fill in the [Pull Request Template](.github/PULL_REQUEST_TEMPLATE.md) checklist.
6. Request a review from `@petesramek`.

**Branch naming convention:**

| Type | Prefix | Example |
|------|--------|---------|
| New feature | `feat/` | `feat/zephyr-adapter` |
| Bug fix | `fix/` | `fix/crc-overflow` |
| Documentation | `docs/` | `docs/update-readme` |
| Refactor | `refactor/` | `refactor/cobs-encoder` |
| Tests | `test/` | `test/edge-cases` |

---

## Development Setup

TinyLink can be built and tested natively (no microcontroller required) via **PlatformIO**.

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (CLI or IDE)
- A C++17-capable compiler (GCC ≥ 7, Clang ≥ 5, or MSVC 2017+)

### Build

```bash
# Compile the native test environment
pio run -e native
```

### Project Layout

```
src/                         Core protocol (TinyLink.h, TinyProtocol.h, version.h)
src/adapters/                Platform adapters (Arduino, POSIX, Windows, ESP-IDF, STM32 HAL)
test/test_native/            PlatformIO-discovered native Unity suite
test/test_native/adapters/   Adapter-specific tests (for example, posix/)
test/test_native/internal/   Codec and internal protocol tests
test/test_native/protocol/   Public protocol state, stats, and type tests
test/test_native/tinylink/   Engine and end-to-end behavior tests
test/test_native/support/    Native-only shared stubs and fixtures
test/compile_contract/       Payload compile probes used by the CI helper
examples/                    Ready-to-run demo sketches
```

---

## Running Tests

Native tests live in the PlatformIO-discovered `test/test_native/` Unity suite and use one
shared runner. Subdirectories group test sources by production responsibility; they do not
create separate runners. Shared native fixtures (`globals.h`, `TinyTestAdapter`, and
`TinyDuplexTestAdapter`) are kept under `test/test_native/support/stubs/` so unrelated suites
do not inherit their headers or include paths. The compile-contract probes are separate from
the Unity suite and are run by `scripts/check_payload_compile_contract.py`.

Run all native tests with:

```bash
pio test -e native
```

Run the same suite with AddressSanitizer and UndefinedBehaviorSanitizer on a
supported GCC or Clang toolchain with:

```bash
pio test -e native_sanitized
```

All tests must pass before a PR will be merged. If you add new functionality, please include corresponding tests.

---

## Code Style

TinyLink follows standard modern C++ conventions. Key points:

- **Standard**: C++17 or newer.
- **Indentation**: 4 spaces (no tabs).
- **Naming**:
  - Classes and structs: `PascalCase` (e.g., `TinyLink`, `TinyArduinoAdapter`)
  - Methods and variables: `camelCase` (e.g., `onReceive`, `getStats`)
  - Constants and macros: `UPPER_SNAKE_CASE` (e.g., `TINYLINK_VERSION`)
- **Headers**: Use `#pragma once` or include guards consistently.
- **Includes**: System headers first, then project headers, separated by a blank line.
- **No dynamic allocation**: Do not use `new`/`delete` or `malloc`/`free` in library code.
- **Template parameters**: Use descriptive names (e.g., `TData`, `TAdapter`).

When in doubt, match the style of the surrounding code.

---

## Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
<type>(<scope>): <short summary>

[optional body]

[optional footer(s)]
```

**Examples:**

```
feat(adapter): add Zephyr RTOS adapter
fix(cobs): handle max-length payload correctly
docs(readme): add polling style example
test(protocol): add checksum error test case
```

Types: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`, `ci`.

---

## Questions?

Open a [Discussion](https://github.com/petesramek/tiny-link/discussions) or reach out via an issue. We are happy to help!
