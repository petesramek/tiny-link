# TinyLink tests

TinyLink's native test suite uses PlatformIO and Unity. Tests exercise public behavior with
deterministic adapters and test-only platform stubs; they do not require hardware or real serial
ports.

## Running tests

Run the complete native suite:

```sh
pio test -e native
```

Run the payload compile-contract checks:

```sh
python tools/compile_contract/check.py
```

The `native` environment uses C++11 with `-Wall -Wextra -Werror -pedantic`. The optional
`native_sanitized` environment adds AddressSanitizer and UndefinedBehaviorSanitizer; sanitizer
runtimes may not be available on every host.

## Test layout

`platformio.ini` sets `test_dir = test/suites`. PlatformIO discovers one suite for each
`test/suites/test_*` directory:

| Suite | Coverage |
| --- | --- |
| `test_adapters_arduino`, `test_adapters_espidf`, `test_adapters_posix`, `test_adapters_stm32`, `test_adapters_windows` | Platform adapter contracts and behavior |
| `test_adapters_write_result` | Adapter write outcomes |
| `test_engine_tinylink` | Connection lifecycle, application frames, ACKs, receive handling, timeouts, recovery, logging, and telemetry |
| `test_internal_codec_cobs`, `test_internal_codec_fletcher16`, `test_internal_codec_packet` | Internal framing and packet codecs |
| `test_protocol_messages`, `test_protocol_stats` | Protocol message codecs, state, message types, ACK results, and statistics |

The TinyLink engine suite separates behavior across focused test files and shares a fixture.
Test support code is under `test/support`: `tinylink/` contains the fake adapter, frame helpers,
diagnostics, and fixture; `stubs/` contains minimal stand-ins for platform SDK types.

## Writing tests

- Keep tests deterministic. Use `FakeAdapter` to control input bytes, captured output, time, and
  transport availability instead of sleeping or accessing real hardware.
- Use production codecs and protocol helpers to construct and inspect frames.
- Assert observable public behavior; avoid private-member access and implementation-specific
  assumptions.
- Use Unity's `*_MESSAGE` assertions with `tinylink_test::diag()` when a loop assertion needs to
  identify the failing input, index, length, or value.
- Keep platform stubs narrow. They model only the API surface used by the adapter tests and are
  not replacements for the platform SDKs.

On Windows, the POSIX adapter suite is ignored because its PTY coverage requires Linux.

## Compile-contract checks

The `tools/compile_contract/check.py` script compiles positive and negative C++11 probes. It
expects a valid payload to compile, a non-trivially-copyable payload to fail at TinyLink's
payload assertion, and a payload larger than 64 bytes to fail at the payload-size assertion.
The script accepts an optional compiler executable; by default it uses `g++`.

## Capacity assumptions

- Maximum application payload: 64 bytes (`PACKET_MAX_PAYLOAD_SIZE`).
- Maximum plain packet: 69 bytes (three-byte header, payload, and two-byte checksum).
- COBS adds encoding overhead; each complete frame also ends with a zero delimiter.
- `FakeAdapter` input and output buffers are capped at 8192 bytes. Clear them when a test queues
  enough frames to approach that capacity.
