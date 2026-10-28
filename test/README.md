# GitHub Copilot Test Generation Review

This review evaluates how GitHub Copilot analyzed TinyLink and generated native C++ tests using PlatformIO and Unity.

## GTP-5.6 Sol assessment

GitHub Copilot generated deterministic tests covering:

- Fletcher-16
- COBS and packet codecs
- protocol messages and types
- statistics and telemetry
- the TinyLink engine
- four platform adapters

The `TinyLink.h` suite covers state transitions, ACK correlation, timeouts, malformed frames, overflow recovery, callbacks, telemetry, and boundary conditions.

The tests reuse production codecs, avoid access to private implementation details, and do not modify production code.

Loop-based assertions now report the failing input, index, or length (see "Failure diagnostics" below).

`TinyPosixAdapter.h` had no test coverage, despite being explicitly included in the prompt. It is now covered by `test/suites/test_adapters_posix`, which uses Linux pseudo-terminals and runs in the Linux native CI job; on Windows hosts it compiles to a single ignored test.

## Personal assessment

I would expect those to be applied by default without explicit definition as a standard for a production-quality test suite:

- The test structure does not reflect the structure under `src`, making related production and test files harder to locate. The flat layout makes the test folder harder to navigate and would not scale well in a larger project.
- Platform stubs are organized inconsistently. The ESP-IDF UART stub uses a `driver` directory, while the other stubs are placed directly in the test root.
- The TinyLink.h tests are concentrated in one large test file instead of being split by responsibility, such as handshake, application traffic, acknowledgements, receive handling, timing, and lifecycle behavior.
- None of the generated test code contains documentation comments. Clear test names describe individual cases, but shared helpers and platform stubs should document their purpose, intended use, limitations, and differences from real platform implementations. Without that context, they are easier to misunderstand or misuse.
- The POSIX adapter was omitted, and the omission was not reported.
- Some of these expectations were not explicitly defined in the repository guidance. This also identified areas where the contribution standards could be more specific.

## Current test layout

`platformio.ini` sets `test_dir = test/suites`; each `test/suites/test_*` directory is one PlatformIO suite (one Unity runner), discovered exactly once.

| Suite | Production responsibility |
| --- | --- |
| `test_adapters_arduino`, `_espidf`, `_posix`, `_stm32`, `_windows` | `src/adapters/Tiny*Adapter.h` |
| `test_adapters_write_result` | `src/adapters/WriteResult.h` |
| `test_internal_codec_cobs`, `_fletcher16`, `_packet` | `src/internal/codec` |
| `test_protocol_messages`, `test_protocol_stats` | `src/protocol`, `src/internal/protocol` |
| `test_engine_tinylink` | `src/TinyLink.h` |

`test_engine_tinylink` has one `test_main.cpp` runner plus behavior files (`lifecycle_tests.cpp`, `application_tests.cpp`, `ack_sequence_tests.cpp`, `receive_mailbox_tests.cpp`, `timeout_tests.cpp`, `overflow_recovery_tests.cpp`, `logging_tests.cpp`, `telemetry_tests.cpp`) and a shared `TinyLinkTestFixture.cpp`. Each behavior file exposes a `register*Tests()` function called by the runner.

Test-only support code lives in `test/support`: `tinylink/` (fake adapter, frame helpers, shared fixture header) and `stubs/<platform>/` (Arduino, ESP-IDF, STM32 HAL, Windows SDK stand-ins, added to the include path in `platformio.ini`).

On Windows hosts `test_adapters_posix` registers a single ignored test; the 17 PTY tests run only on Linux.

## Test infrastructure reference

### Shared helpers (`test/support/tinylink`)

- `FakeAdapter.h`: deterministic in-memory adapter (inbound queue, captured output, manual clock, scripted partial/failed writes, `setOpen`). Input and output are each capped at 8192 bytes; `scriptWrites` holds 8 entries.
- `TinyLinkTestHelpers.h`: `queuePacket`/`queueApplication`/`queueHandshake`/`queueAck` build inbound frames with the production COBS and packet codecs (never hand-encoded); `writtenFrameCount`/`decodeWrittenFrame` decode what the engine wrote.
- `TinyLinkTestFixture.h` (+ `test_engine_tinylink/TinyLinkTestFixture.cpp`): `Payload` (4 bytes), `MaximumPayload` (64 bytes), callback recorders (reset with `resetCallbacks()` in `setUp`), `connectPeer`, and a `TinyLinkStatsAccess` friend used to read stats.
- `TestDiagnostics.h`: `diag(fmt, ...)` formats failure context for `TEST_ASSERT_*_MESSAGE`.

### Failure diagnostics

Assertions inside loops must say which input failed. Use the `_MESSAGE` variants with `tinylink_test::diag`, naming the index, length, or value under test, e.g. `TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected, actual, diag("input length=%u", (unsigned)length))`. `diag` returns a single static buffer overwritten on the next call, so use it inline or store the pointer only until the next `diag` call. When a loop makes several assertions about one case, build the context once at the top of the iteration.

### Platform stubs (`test/support/stubs/<platform>`)

Minimal stand-ins for the Arduino, ESP-IDF UART, STM32 HAL, and Windows serial APIs, added to the include path in `platformio.ini`. They are scripted through file-scope variables and are not SDK replacements: they model only what the adapters call, never real timing, buffering, or errors beyond what a test sets. State is not reset automatically (the Windows stub provides `tinylink_reset_windows_stub()`), so reset it per test.

### Compile-contract probes (`tools/compile_contract`)

Unity cannot assert that code fails to compile, so `python tools/compile_contract/check.py [compiler]` (default `g++`, `-std=gnu++11 -fsyntax-only`) compiles each probe: `payload_valid.cpp` must compile; `payload_not_trivially_copyable.cpp` must fail with "must be trivially copyable"; `payload_too_large.cpp` must fail with "exceeds 64 bytes". The script exits non-zero if any case deviates.

### Strict and sanitized builds

The `native` environment compiles with `-std=c++11 -Wall -Wextra -Werror -pedantic`. `native_sanitized` extends it with `-fsanitize=address,undefined` and runs in the Linux CI job; it needs sanitizer runtimes unavailable on some hosts (e.g. Windows/Clang).

### Capacity assumptions

- Maximum application payload: 64 bytes (`PACKET_MAX_PAYLOAD_SIZE`); packet = 3-byte header + payload + 2-byte checksum = at most 69 bytes (`PACKET_MAX_SIZE`).
- COBS adds one byte per 254 bytes plus one, and a frame ends with a `0x00` delimiter, so encode buffers in helpers are `PACKET_MAX_SIZE + PACKET_MAX_SIZE / 254 + 1`.
- `FakeAdapter` buffers: 8192 bytes each way; tests that queue many frames should `clearInput()`/`clearOutput()`.

## Prompts and pull requests

All three prompts were run sequentially in one GitHub Copilot session. Each prompt continued from the repository state produced by the previous pull request.

### Session details

- Mode: Autopilot
- Model: Auto (Mostly GPT-6.0 Astra from what I saw)
- Reasoning: Balanced
- AI credits used: 25.9
- Cached tokens: 13.6M
- Reasoning tokens: 113.3K

### Fletcher-16 test infrastructure and coverage

<details>
<summary>Full prompt</summary>

```text
Review the current TinyLink repository and add the initial native test infrastructure under the existing `test` folder.

Start by inspecting:

- the repository structure
- `platformio.ini`
- `src/internal/codec/Fletcher16.h`
- any existing test configuration

Set up PlatformIO native tests using Unity and create a comprehensive test suite for the current Fletcher-16 implementation.

The tests should cover the documented contract, known vectors, important boundaries, edge cases, and broader deterministic verification. Where appropriate, use an independent reference implementation rather than repeating the production algorithm.

Keep the work limited to Fletcher-16 and the minimum infrastructure needed to run its tests.

Do not:

- modify production code
- add tests for other components
- add unrelated helpers or dependencies
- add a `main.cpp` under `src`
- add CI configuration
- make unrelated cleanup changes

Use C++11 and follow the repository’s existing style and documentation conventions.

Run the completed tests with:

`pio test -e native`

Fix any issues related to the test infrastructure or Fletcher-16 suite, then report:

- files created or modified
- test cases added
- the command executed
- the final test result
- any assumptions or discrepancies found

Stop after the complete Fletcher-16 test suite passes.
```

</details>

**Result:** [PR #32](https://github.com/petesramek/tiny-link/pull/32)

### TinyLink engine coverage

<details>
<summary>Full prompt</summary>

```text
Review the current TinyLink repository and create a comprehensive native test suite for:

`src/TinyLink.h`

Before making changes, inspect:

- `platformio.ini`
- the repository structure
- `src/TinyLink.h`
- all headers directly used by `TinyLink.h`
- the existing tests, including the Fletcher-16 suite
- the current protocol, codec, state, telemetry, and adapter contracts

Design the tests from the documented and observable behavior of `TinyLink.h`.

Create any minimal test-only infrastructure needed under the existing `test` folder, including a deterministic fake adapter if appropriate. The fake adapter should allow tests to control incoming bytes, inspect outgoing bytes, advance time without sleeping, and change transport availability.

Use the real production codecs and protocol helpers when constructing or inspecting frames. Do not duplicate the packet, COBS, checksum, ACK, handshake, or log encoding algorithms in the tests.

Create a comprehensive test suite that covers the important behavior of `TinyLink.h`, including:

- construction and initial state
- connection and handshake behavior
- application payload transmission and reception
- sequence allocation and ACK correlation
- wrong-sequence, stale, malformed, and unsolicited ACK handling
- reserved internal message routing
- log and handshake frame handling
- callback and polling behavior
- connection, receive, and transmit state independence
- inter-byte timeout behavior
- ACK timeout behavior
- handshake retry behavior
- malformed frames
- invalid checksums
- receive overflow and discard-until-delimiter recovery
- maximum supported payloads
- rejected application message types
- statistics and telemetry changes
- reset behavior
- timer wraparound where practical
- any other meaningful boundary or edge cases discovered during repository analysis

Keep the tests deterministic. Do not use real serial devices, real waiting, threads, random values, or wall-clock timing.

Focus on observable public behavior. Avoid accessing private members, changing access modifiers, using preprocessor tricks to expose internals, or coupling tests to implementation details unnecessarily.

Constraints:

- Do not modify production code.
- Do not modify the public API.
- Do not add tests for unrelated production files except where their real APIs are required to exercise `TinyLink.h`.
- Do not duplicate the existing Fletcher-16 test suite.
- Do not add third-party dependencies beyond PlatformIO’s existing Unity test framework.
- Do not add `main.cpp` under `src`.
- Do not add CI configuration.
- Do not use heap allocation unless the current production contract genuinely requires it.
- Do not use exceptions.
- Do not suppress warnings.
- Do not make unrelated cleanup changes.
- Use C++11.
- Follow the repository’s existing style and documentation conventions.

Organize the tests so they remain readable and maintainable. Prefer focused test cases with clear responsibilities over a few very large scenarios. Share test-only helpers where doing so reduces duplication without hiding the intent of individual tests.

Run the complete native test suite with:

`pio test -e native`

If failures occur:

1. Determine whether the problem is in the new tests, the test infrastructure, or the current production behavior.
2. Fix only mistakes in the tests or test infrastructure.
3. Do not alter production code to make a test pass.
4. If a test exposes a likely production defect or an unclear contract, leave production code unchanged and report it precisely.
5. Run the full native suite again after corrections.

Before finishing, review every created or modified test file for:

- duplicated logic
- accidental reimplementation of production codecs
- invalid assumptions about wire format
- dependence on private implementation details
- nondeterministic timing
- weak assertions
- missing cleanup between test cases
- tests that pass without exercising the intended behavior

Report:

- files created
- files modified
- test-only infrastructure added
- all test cases or tested behavior groups
- the exact command executed
- the final test result
- any assumptions made
- any unclear contracts found
- any suspected production defects found
- any important behavior that could not be tested and why

Stop after the comprehensive `TinyLink.h` native test suite passes, or after reporting a specific production defect that prevents a valid test from passing. Do not continue by generating tests for the rest of the repository.
```

</details>

**Result:** [PR #33](https://github.com/petesramek/tiny-link/pull/33)

### Remaining native coverage

<details>
<summary>Full prompt</summary>

```text
Create a new branch and do this work, then create PR once you finish:

Review the current TinyLink repository and complete the remaining native test coverage in one pass.

The repository already contains comprehensive native tests for:

- `src/internal/codec/Fletcher16.h`
- `src/TinyLink.h`

Do not duplicate those tests.

Before making changes, inspect:

- `platformio.ini`
- the complete `src` directory
- the existing `test` directory
- all current production headers
- existing test helpers and conventions
- the current PlatformIO and Unity configuration

Identify every production component that is not already covered adequately by the Fletcher-16 and TinyLink test suites.

Create comprehensive native Unity tests for the remaining platform-neutral components, including where applicable:

- `src/internal/codec/CobsCodec.h`
- `src/internal/codec/Packet.h`
- `src/internal/protocol/AckMessage.h`
- `src/internal/protocol/HandshakeMessage.h`
- `src/internal/protocol/LogMessage.h`
- `src/protocol/AckResult.h`
- `src/protocol/LogLevel.h`
- `src/protocol/MessageType.h`
- `src/protocol/State.h`
- `src/protocol/Stats.h`
- `src/protocol/TelemetryEvent.h`
- `src/Version.h`

Also add compile-contract tests for the adapter headers where they can be tested safely in the native environment.

The adapter scope includes:

- `src/adapters/TinyArduinoAdapter.h`
- `src/adapters/TinyESPIDFAdapter.h`
- `src/adapters/TinyPosixAdapter.h`
- `src/adapters/TinySTM32HALAdapter.h`
- `src/adapters/TinyWindowsAdapter.h`

For platform-specific adapters:

- Do not require real hardware or serial devices.
- Do not make native tests depend on installed Arduino, ESP-IDF, STM32 HAL, or Windows SDK environments unless the current native environment already provides them.
- Use minimal test-only platform stubs where appropriate.
- Keep those stubs under the `test` folder.
- Validate the public adapter contract, compilation, ownership rules, basic success behavior, closed or unavailable behavior, length boundaries, and timing semantics where practical.
- Do not attempt to fake complete platform SDKs.
- If an adapter cannot be tested meaningfully in the current native environment, add the narrowest reasonable compile-contract coverage and report the limitation.

Test the documented and observable behavior of each component.

For codecs and protocol messages, cover:

- valid values
- known wire representations
- exact buffer capacities
- insufficient capacities
- null-pointer contracts
- empty-input behavior where supported
- malformed input
- invalid enum or wire values
- output values remaining unchanged after failure
- complete round trips
- relevant minimum and maximum lengths
- byte order
- overflow protection
- boundary conditions
- independent deterministic verification where appropriate

For `CobsCodec.h`, include strong coverage of:

- empty input
- zero-containing input
- long nonzero runs
- 254-byte block boundaries
- malformed encoded blocks
- exact capacity calculations
- output remaining unchanged on failure
- broad deterministic round trips

For `Packet.h`, include strong coverage of:

- empty and maximum payloads
- exact wire layout
- raw application type preservation
- checksum coverage and byte order
- malformed lengths
- short and oversized frames
- checksum corruption
- validation-only decoding
- all output values remaining unchanged on failure
- round trips across every supported payload length

For `Stats.h` and `TelemetryEvent.h`, include:

- zero initialization
- every declared telemetry event
- correct event-to-counter mapping
- clear/reset behavior
- copy and snapshot independence
- merge behavior
- counter saturation
- string formatting and capacity handling
- undeclared event behavior where representable

For enums, version declarations, and state helpers, include:

- stable declared wire values
- valid and invalid conversions
- destination unchanged on failed conversions
- diagnostic string behavior
- state helper predicates
- separation of library release version and protocol version
- important compile-time invariants

Test organization:

- Keep suites grouped by production responsibility.
- Use separate test-suite folders where that improves clarity.
- Reuse existing test-only helpers where appropriate.
- Add shared helpers only when they remove meaningful duplication.
- Keep individual tests focused and clearly named.
- Avoid one very large test function.
- Avoid unnecessary duplication between suites.

Use production codecs and conversion helpers wherever the test is exercising a higher-level component.

Only use an independent reference implementation when the component itself is the algorithm being verified. Do not duplicate production packet, COBS, message, or serialization logic merely to construct test data.

Constraints:

- Do not modify production code.
- Do not modify the public API.
- Do not change documented protocol behavior.
- Do not add tests that depend on private members.
- Do not use preprocessor tricks to expose private members.
- Do not add `main.cpp` under `src`.
- Do not add CI configuration.
- Do not add external test frameworks or unnecessary dependencies.
- Do not use real serial ports, hardware, sleeping, threads, random input, or wall-clock timing.
- Do not suppress warnings.
- Do not make unrelated cleanup changes.
- Use C++11.
- Follow the repository’s existing formatting and documentation conventions.
- Keep all tests deterministic and reproducible.

Run the complete native suite with:

`pio test -e native`

If failures occur:

1. Determine whether the failure is caused by the new test, test infrastructure, an unclear contract, or a likely production defect.
2. Fix only mistakes in tests or test infrastructure.
3. Do not modify production code to make tests pass.
4. If a valid test exposes a likely production defect, leave production code unchanged and report the defect precisely.
5. Run the complete native suite again after every correction.

Before finishing, review all created or modified test files for:

- false-positive tests
- weak or missing assertions
- duplicated production logic
- accidental dependence on private implementation details
- invalid wire-format assumptions
- nondeterminism
- state leaking between tests
- platform stubs that are broader than necessary
- tests whose names do not match what they actually prove
- important untested public behavior

Report:

- production components covered
- files created
- files modified
- shared test infrastructure added
- test suites and behavior groups added
- the exact command executed
- the total number of tests
- the final pass/fail result
- assumptions made
- limitations of adapter testing
- unclear contracts found
- suspected production defects found
- production components intentionally left untested and why

Stop after all valid native tests pass, or after reporting a specific production defect that prevents a valid test from passing.

Do not modify production code and do not continue into CI, examples, documentation, or hardware integration tests.
```

</details>

**Result:** [PR #34](https://github.com/petesramek/tiny-link/pull/34)
