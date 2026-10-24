# Review of `src/protocol/LogLevel.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/LogLevel.h`

`LogLevel.h` is sound. The enum already has an explicit `uint8_t` underlying type and stable numeric values suitable for a one-byte wire field. No direct correctness or memory-safety defect is visible in this file.

The remaining work is to validate raw wire values before converting them to `LogLevel`, lock down the numeric wire contract, and document how future levels may be added.

---

# Summary

## Positive assessment

1. `LogLevel` has an explicit one-byte underlying type.
2. Every level has an explicit numeric value.
3. Valid values are contiguous from `0` through `4`.
4. Numeric order matches documented severity order.
5. The `@file` path matches the source-root-relative repository path.

## Required improvements

1. Validate raw incoming bytes before conversion to `LogLevel`.
2. Lock down the enum size and numeric values through compile-time assertions or protocol tests.
3. Define whether severity ordering is a stable public contract.
4. Define a forward-extension policy for new levels.
5. Treat naming changes separately from protocol remediation because they would break source compatibility.

---

# Positive findings

## 1. Explicit one-byte representation

The enum declares its underlying type directly:

```cpp
enum class LogLevel : uint8_t {
    // Values
};
```

This guarantees an unsigned one-byte representation suitable for the existing log wire field.

A direct assertion would make that wire requirement explicit:

```cpp
static_assert(sizeof(LogLevel) == 1,
              "LogLevel must have an 8-bit representation");
```

### Assessment

No change to the underlying type is needed.

---

## 2. Explicit numeric wire values

Every level has a fixed numeric value:

```cpp
TRACE = 0,
DEBUG = 1,
INFO  = 2,
WARN  = 3,
ERROR = 4
```

This is important because `LogLevel` values are transmitted over the wire. Reordering the declarations will not silently change their encoded values.

### Assessment

The explicit assignments should be retained permanently.

---

## 3. Contiguous valid range

The currently valid raw range is:

```text
0 through 4
```

This permits compact validation while the enum remains contiguous:

```cpp
value <= static_cast<uint8_t>(LogLevel::ERROR)
```

An explicit `switch` is more future-proof if gaps, reserved values, or aliases may be introduced later.

---

## 4. Numeric order matches severity order

The documentation states that values progress from least severe to most severe:

```text
TRACE < DEBUG < INFO < WARN < ERROR
```

The numeric values support severity threshold filtering:

```cpp
if (static_cast<uint8_t>(level) >=
    static_cast<uint8_t>(minimumLevel)) {
    // Emit the log message.
}
```

If consumers are expected to rely on this, numeric severity ordering must be documented as a stable contract.

---

## 5. The `@file` path is consistent

The header declares:

```cpp
@file protocol/LogLevel.h
```

The reviewed path is:

```text
src/protocol/LogLevel.h
```

This is consistent with a source-root-relative documentation convention.

The same convention should be applied to the internal headers reviewed earlier:

```text
internal/protocol/LogMessage.h
internal/protocol/AckMessage.h
internal/protocol/HandshakeMessage.h
internal/protocol/Packed.h
```

---

# P1: Validate incoming raw values

## 1. Not every wire byte is a valid `LogLevel`

### Current behavior

The wire field can contain any value from `0` through `255`, but the enum currently defines only five values.

The following values are not declared levels:

```text
5
127
255
```

The current log receive path copies the raw level byte into the message and invokes the callback without validating it.

### Required change

Add a validation helper and use it before converting raw bytes to `LogLevel`.

A compact implementation is valid while the enum remains contiguous:

```cpp
inline bool is_valid_log_level(uint8_t value)
{
    return value <= static_cast<uint8_t>(LogLevel::ERROR);
}
```

A more extension-safe implementation uses an explicit `switch`:

```cpp
inline bool is_valid_log_level(uint8_t value)
{
    switch (value) {
        case static_cast<uint8_t>(LogLevel::TRACE):
        case static_cast<uint8_t>(LogLevel::DEBUG):
        case static_cast<uint8_t>(LogLevel::INFO):
        case static_cast<uint8_t>(LogLevel::WARN):
        case static_cast<uint8_t>(LogLevel::ERROR):
            return true;

        default:
            return false;
    }
}
```

Convert only after validation:

```cpp
if (!is_valid_log_level(rawLevel)) {
    return LogDecodeStatus::InvalidLevel;
}

const LogLevel level = static_cast<LogLevel>(rawLevel);
```

### Recommendation

Use the explicit `switch`. The implementation is slightly longer but will not silently broaden the valid range if future values become noncontiguous.

### Acceptance criteria

- Raw values are validated before conversion.
- Values `0` through `4` are accepted.
- Values `5` through `255` are rejected unless explicitly introduced later.
- Invalid levels do not invoke the successful log callback.
- Invalid levels produce a specific diagnostic.
- Unknown values cannot silently enter severity filtering or callback switches.

### Required tests

- Raw `0` becomes `TRACE`.
- Raw `1` becomes `DEBUG`.
- Raw `2` becomes `INFO`.
- Raw `3` becomes `WARN`.
- Raw `4` becomes `ERROR`.
- Raw `5` is rejected.
- Raw `127` is rejected.
- Raw `255` is rejected.

---

# P1: Lock down the wire contract

## 2. Add representation and value assertions

### Problem

The enum is already defined correctly, but its size and numeric values are part of the protocol contract. An accidental edit could silently change wire compatibility.

### Required change

Add compile-time assertions in the header or dedicated protocol contract tests:

```cpp
static_assert(sizeof(LogLevel) == 1,
              "LogLevel must have an 8-bit representation");

static_assert(static_cast<uint8_t>(LogLevel::TRACE) == 0,
              "LogLevel::TRACE wire value changed");
static_assert(static_cast<uint8_t>(LogLevel::DEBUG) == 1,
              "LogLevel::DEBUG wire value changed");
static_assert(static_cast<uint8_t>(LogLevel::INFO) == 2,
              "LogLevel::INFO wire value changed");
static_assert(static_cast<uint8_t>(LogLevel::WARN) == 3,
              "LogLevel::WARN wire value changed");
static_assert(static_cast<uint8_t>(LogLevel::ERROR) == 4,
              "LogLevel::ERROR wire value changed");
```

If assertion noise is undesirable in the public header, put value assertions in protocol contract tests. The one-byte size assertion is still useful close to the declaration.

### Acceptance criteria

- Changing an existing numeric value causes a clear compile or test failure.
- The one-byte representation is verified.
- Numeric values are documented as stable wire identifiers.
- New levels always receive explicit numeric assignments.

---

# P1: Define numeric ordering as a contract

## 3. Decide whether severity comparisons are supported

### Current behavior

Numeric order follows severity order. This makes threshold filtering straightforward.

However, the header does not explicitly state whether consumers may rely on numeric comparison as a stable API behavior beyond the descriptive ordering comment.

### Required decision

Choose one policy.

## Ordered severity contract

Document that numeric values increase with severity and preserve that rule for all future additions.

This supports code such as:

```cpp
bool should_log(LogLevel level, LogLevel minimum)
{
    return static_cast<uint8_t>(level) >=
        static_cast<uint8_t>(minimum);
}
```

## Identifier-only contract

Treat the numeric values only as wire identifiers. Consumers must use explicit logic rather than comparisons.

### Recommendation

Keep severity ordering as a stable contract. The current values already model it clearly, and threshold filtering is a natural feature for logging.

### Acceptance criteria

- The documentation states whether numeric comparisons are supported.
- Future levels preserve ordering if comparisons are supported.
- Tests verify each ordering relationship.
- No future level insertion renumbers an existing value.

---

# P2: Public naming consistency

## 4. Uppercase members are valid but not idiomatic modern C++

### Current behavior

The public members are:

```cpp
LogLevel::TRACE
LogLevel::DEBUG
LogLevel::INFO
LogLevel::WARN
LogLevel::ERROR
```

Because this is an `enum class`, many modern C++ codebases prefer PascalCase members:

```cpp
LogLevel::Trace
LogLevel::Debug
LogLevel::Info
LogLevel::Warn
LogLevel::Error
```

### Assessment

This is not a correctness problem.

Renaming the members would break source compatibility even if numeric wire values remained unchanged.

### Recommendation

Do not rename these values as part of protocol remediation. If the repository needs a public enum naming convention, decide it once and apply it consistently before a stable release.

### Acceptance criteria

- Protocol fixes do not introduce unrelated source-level renames.
- Any future naming change is treated as a deliberate public API change.
- Wire values remain unchanged if names are ever revised.

---

## 5. Decide whether `WARN` or `WARNING` is canonical

`WARN` is a common logging label and is clear in context. It is an abbreviation while the other members are complete words, but this is stylistic rather than defective.

### Recommendation

Keep `WARN` unless the public API naming is deliberately revised before stabilization.

---

# P2: Define future extension rules

## 6. Preserve existing values when adding levels

### Problem

Future additions can accidentally invalidate assumptions about stable wire identifiers or numeric severity ordering.

For example:

```cpp
FATAL = 5
```

preserves the current order.

Adding a level between existing values cannot renumber the existing wire identifiers. A new intermediate severity would need a deliberately selected value and possibly a revised comparison model or protocol version.

### Required documentation

State that:

- existing numeric values are stable wire identifiers
- existing values must never be renumbered
- every new level receives an explicit value
- numeric order represents severity if that contract is retained
- unknown values are rejected unless a future compatibility policy says otherwise
- wire-impacting changes require protocol compatibility review

### Acceptance criteria

- Existing values are never renumbered.
- New levels use explicit assignments.
- Severity ordering remains valid or is intentionally replaced.
- Compatibility impact is reviewed before adding new values.
- Raw-value validation is updated alongside the enum.

---

# Required test suite

## Representation tests

- [ ] `sizeof(LogLevel) == 1`.
- [ ] `LogLevel` uses `uint8_t` as its underlying type.
- [ ] `TRACE` equals `0`.
- [ ] `DEBUG` equals `1`.
- [ ] `INFO` equals `2`.
- [ ] `WARN` equals `3`.
- [ ] `ERROR` equals `4`.

## Raw-value validation

- [ ] Raw `0` is accepted as `TRACE`.
- [ ] Raw `1` is accepted as `DEBUG`.
- [ ] Raw `2` is accepted as `INFO`.
- [ ] Raw `3` is accepted as `WARN`.
- [ ] Raw `4` is accepted as `ERROR`.
- [ ] Raw `5` is rejected.
- [ ] Raw `127` is rejected.
- [ ] Raw `255` is rejected.

## Severity ordering

If numeric ordering is part of the contract, verify:

- [ ] `TRACE < DEBUG`.
- [ ] `DEBUG < INFO`.
- [ ] `INFO < WARN`.
- [ ] `WARN < ERROR`.
- [ ] Threshold filtering includes equal and more-severe levels.
- [ ] Threshold filtering excludes less-severe levels.

## Log codec integration

- [ ] Each valid level encodes to the expected byte.
- [ ] Each valid byte decodes to the expected level.
- [ ] Invalid level prevents successful `LogMessage` decoding.
- [ ] Invalid level does not invoke the log callback.
- [ ] Invalid level produces the correct diagnostic.
- [ ] Level validation is independent of text validity.
- [ ] Golden log vectors contain the expected level byte at the documented offset.

## Cross-platform verification

- [ ] Identical level values are produced on AVR.
- [ ] Identical level values are produced on ARM.
- [ ] Identical level values are produced on ESP32.
- [ ] Identical level values are produced with GCC.
- [ ] Identical level values are produced with Clang.
- [ ] Identical level values are produced with MSVC.

---

# Implementation checklist

## Representation

- [ ] Add `sizeof(LogLevel) == 1` verification.
- [ ] Add fixed wire-value assertions or protocol contract tests.
- [ ] Keep the explicit `uint8_t` underlying type.
- [ ] Keep explicit numeric assignments.

## Validation

- [ ] Add `is_valid_log_level(uint8_t)`.
- [ ] Prefer explicit `switch` validation for future extension safety.
- [ ] Validate before `static_cast<LogLevel>`.
- [ ] Return a specific invalid-level decode status.
- [ ] Prevent callbacks for invalid values.

## Contract

- [ ] Document numeric values as stable wire identifiers.
- [ ] Decide whether numeric comparison is a stable severity contract.
- [ ] Document future extension rules.
- [ ] Require explicit values for all future members.
- [ ] Update validation whenever a new level is added.

## Naming

- [ ] Do not rename members during protocol remediation.
- [ ] Decide repository-wide enum naming separately if needed.
- [ ] Treat any rename as a public source API change.

## Documentation

- [ ] Keep `@file protocol/LogLevel.h`.
- [ ] Apply the same source-root-relative `@file` convention to internal headers.
- [ ] Document rejection of unknown wire values.

---

# Definition of done

The `LogLevel.h` remediation is complete when:

- [ ] The enum remains explicitly backed by `uint8_t`.
- [ ] Its one-byte representation is verified.
- [ ] Every existing numeric value is locked down.
- [ ] Raw input is validated before conversion.
- [ ] Undefined values are rejected through a specific decode result.
- [ ] Invalid values cannot reach normal callbacks or severity filtering.
- [ ] Severity ordering is either documented as stable or explicitly unsupported.
- [ ] Future extension rules prevent renumbering existing values.
- [ ] Naming remains stable unless revised as a deliberate public API change.
- [ ] Representation, validation, ordering, codec integration, and cross-platform tests pass.

---

# Verdict

`LogLevel.h` is correctly designed for a one-byte wire field:

- explicit `uint8_t` underlying type
- explicit numeric values
- contiguous severity ordering
- no layout or endianness concern

No structural rewrite is needed.

The required protocol correction is outside the enum declaration itself: validate raw incoming bytes before converting them to `LogLevel`. The enum should otherwise remain stable while the log wire codec is migrated from packed-structure transmission to explicit encoding.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`
