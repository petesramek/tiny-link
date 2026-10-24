# Review of `src/protocol/Status.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`

`Status.h` confirms the broader modeling problem identified in `Stats.h`. `TinyStatus` is documented as an ACK result, but several values represent local receive and transport failures that are not meaningful as peer operation results.

The enum itself is mechanically sound:

- explicit `uint8_t` underlying type
- explicit stable wire values
- diagnostic fallback for invalid enum objects

The main correction is conceptual. ACK results and local engine diagnostics should not share one enum.

---

# Summary

## Positive findings

1. `TinyStatus` has an explicit one-byte representation.
2. Every declared value has an explicit wire identifier.
3. `ERR_UNKNOWN` is intentionally separated at `0xFF`.
4. `tinystatus_to_string()` safely handles invalid enum objects.
5. No memory-safety defect exists in this file.

## Main problems

1. ACK results and local protocol events are conflated.
2. `ERR_CRC`, `ERR_TIMEOUT`, and `ERR_OVERFLOW` have unclear ACK semantics.
3. `ERR_CRC` combines checksum and COBS failures.
4. `ERR_TIMEOUT` is documented as an inter-byte timeout but is also used for ACK timeout.
5. `ERR_OVERFLOW` describes local parser state, not necessarily a peer operation result.
6. Raw wire values are not validated before becoming `TinyStatus`.
7. `ERR_UNKNOWN` and invalid raw status values can be conflated.
8. Stable wire identifiers are not protected by assertions.
9. Naming cleanup would be source-breaking and should follow the model correction.
10. The `@file` path should follow the repository's source-root-relative convention.

---

# P0: Separate ACK results from local engine diagnostics

## 1. `TinyStatus` currently represents two different domains

### Current intent

The file describes `TinyStatus` as:

> ACK/error codes carried in TinyAck frames.

Some values work naturally as ACK results:

```cpp
STATUS_OK
ERR_BUSY
ERR_PROCESSING
ERR_UNKNOWN
```

These can describe how a peer handled an identified application request.

Other values describe local engine events:

```cpp
ERR_CRC
ERR_TIMEOUT
ERR_OVERFLOW
```

The current engine uses these for events such as:

- malformed COBS input
- checksum failure
- partial-frame timeout
- ACK timeout
- receive-buffer overflow
- local packet-packing failure

Those events are not all results of processing an acknowledged application message.

### Why this matters

An ACK result should answer:

```text
What happened when the peer processed sequence 42?
```

Local diagnostics answer different questions:

```text
Did this endpoint fail to decode a frame?
Did its receive buffer overflow?
Did it wait too long for an ACK?
```

Combining both domains makes callback behavior, statistics, and wire semantics ambiguous.

### Required change

Create separate concepts.

## Wire-level ACK result

For example:

```cpp
enum class AckResult : uint8_t {
    Ok         = 0x00,
    Busy       = 0x04,
    Processing = 0x05,
    Unknown    = 0xFF
};
```

Additional peer-processing outcomes can be added when their semantics are defined.

## Local telemetry event

For example:

```cpp
enum class TelemetryEvent : uint8_t {
    PacketReceived,
    CobsDecodeFailed,
    ChecksumMismatch,
    PacketLengthMismatch,
    PayloadSizeMismatch,
    InterByteTimeout,
    AckTimeout,
    ReceiveOverflow,
    UnexpectedMessageType,
    UnexpectedSequence,
    InvalidHandshake
};
```

`TelemetryEvent` does not need to be transmitted unless TinyLink deliberately introduces a diagnostic-reporting protocol.

### Acceptance criteria

- ACK results describe peer handling of an acknowledged frame.
- Local decode and parser failures have a separate type.
- Statistics no longer use ACK results as local telemetry keys.
- ACK callbacks expose only meaningful peer outcomes.
- Local failures cannot be transmitted as application ACK results without an explicit mapping.

---

# P0: Define negative ACK semantics

## 2. CRC, timeout, and overflow do not naturally identify an acknowledged operation

### Current behavior

The enum permits calls such as:

```cpp
sendAck(TinyStatus::ERR_CRC);
sendAck(TinyStatus::ERR_TIMEOUT);
sendAck(TinyStatus::ERR_OVERFLOW);
```

However, `sendAck()` acknowledges the last successfully dispatched application frame.

If a frame suffered checksum failure, timeout, or receive overflow, the engine may not have a trustworthy or complete application sequence to acknowledge.

### Sequence trust problem

A checksum failure means the sequence byte itself cannot necessarily be trusted because it was protected by the failed checksum.

Sending:

```text
ACK(sequence, CRC failure)
```

would require using metadata from a frame that failed integrity verification.

Similarly:

- an inter-byte timeout may occur before a complete sequence is available
- overflow may discard the frame before its identity is known
- a local ACK timeout concerns an outgoing frame and is not a peer processing result

### Required decision

For every status, define whether it is:

1. a valid ACK/NACK result
2. a local-only telemetry event
3. a separate protocol diagnostic message

### Recommendation

Move these values out of ACK results:

```cpp
ERR_CRC
ERR_TIMEOUT
ERR_OVERFLOW
```

If remote reporting of malformed input is required, introduce a separate diagnostic frame with semantics that do not assume a trusted original sequence.

### Acceptance criteria

- Every ACK result applies to a successfully identified application sequence.
- The engine never acknowledges a frame whose sequence cannot be trusted.
- Receive corruption, timeout, and overflow are recorded locally.
- Remote diagnostics, if needed, use a separately defined message.

---

# P1: Split COBS and checksum failures

## 3. `ERR_CRC` describes several unrelated failures

### Current comment

```cpp
ERR_CRC = 0x01
```

means:

> Fletcher-16 checksum or COBS framing failure.

COBS failure and checksum failure happen at different layers:

```text
COBS failure
    -> encoded frame is malformed

checksum failure
    -> frame decoded structurally, but integrity verification failed
```

The current engine also uses `ERR_CRC` for local packet-packing failure, which is neither a framing failure nor a checksum mismatch.

### Required change

Use precise local events:

```cpp
CobsDecodeFailed
ChecksumMismatch
PacketEncodeFailed
PacketLengthMismatch
OutputBufferTooSmall
```

If one aggregate counter is intentionally retained, use a broader name such as:

```cpp
FrameDecodeFailed
```

Typed codec results now provide enough information to maintain the actual cause.

### Acceptance criteria

- COBS input failure is distinguishable from checksum mismatch.
- Local encoding failure is not described as CRC failure.
- Packet-length failure has its own event.
- Statistics identify the failing layer accurately.

---

# P1: Split timeout meanings

## 4. `ERR_TIMEOUT` has inconsistent semantics

### Current documentation

```cpp
ERR_TIMEOUT ///< Inter-byte timeout triggered
```

### Actual use

`TinyLink.h` increments it for:

- a stalled partial receive frame
- a missing ACK after sending data

These are independent timers with different causes.

### Required change

Use separate local telemetry events:

```cpp
InterByteTimeout
AckTimeout
HandshakeTimeout
```

The handshake event is needed only if handshake failure timing is introduced.

An ACK result named `Timeout` should exist only if it describes a defined peer-side application-processing outcome.

### Acceptance criteria

- Inter-byte and ACK timeouts have different counters.
- Timeout diagnostics identify the affected operation.
- `TinyStatus` no longer doubles as a local timer event.
- Documentation matches every event-reporting location.

---

# P1: Validate raw status bytes

## 5. Not every byte is a declared status

### Current declared values

```text
0x00
0x01
0x02
0x03
0x04
0x05
0xFF
```

Representative undefined values include:

```text
0x06
0x7F
0xFE
```

The current ACK receiver copies raw payload bytes directly into `TinyAck`, which contains a `TinyStatus` field.

### Required change

Validate raw values before conversion.

A transitional helper for the current enum could be:

```cpp
inline bool tiny_status_from_wire(
    uint8_t value,
    TinyStatus& output)
{
    switch (value) {
        case static_cast<uint8_t>(TinyStatus::STATUS_OK):
            output = TinyStatus::STATUS_OK;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_CRC):
            output = TinyStatus::ERR_CRC;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_TIMEOUT):
            output = TinyStatus::ERR_TIMEOUT;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_OVERFLOW):
            output = TinyStatus::ERR_OVERFLOW;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_BUSY):
            output = TinyStatus::ERR_BUSY;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_PROCESSING):
            output = TinyStatus::ERR_PROCESSING;
            return true;

        case static_cast<uint8_t>(TinyStatus::ERR_UNKNOWN):
            output = TinyStatus::ERR_UNKNOWN;
            return true;

        default:
            return false;
    }
}
```

After the model is separated, validate only actual ACK result values in the ACK decoder.

### Acceptance criteria

- Raw bytes are validated before conversion.
- Undefined values do not enter callbacks.
- Undefined values do not clear pending ACK state.
- Failed conversion leaves output unchanged.
- Declared unknown result and invalid raw input remain distinct.

---

# P1: Keep declared unknown distinct from invalid input

## 6. `0xFF` is a legitimate status value

### Current behavior

```cpp
ERR_UNKNOWN = 0xFF
```

This makes `0xFF` a valid declared result representing an unspecified or unrecognized operation error.

A raw byte such as `0x7F` is different. It is not a declared result.

### Required behavior

```text
0xFF -> valid declared Unknown result
0x7F -> invalid or unsupported wire value
```

The current `TinyStats` implementation can route both through the same `unknown` counter if invalid enum objects reach it.

### Acceptance criteria

- Declared unknown result is accepted.
- Invalid raw values are rejected or represented separately.
- Statistics distinguish peer-reported unknown from malformed status input.
- Diagnostic strings distinguish `ERR_UNKNOWN` from `INVALID`.

---

# P1: Lock down wire values

## 7. Add representation and numeric-value assertions

The enum already uses explicit values. Because those values are part of the wire contract, they should be protected.

### Suggested assertions

```cpp
static_assert(sizeof(TinyStatus) == 1,
              "TinyStatus must have an 8-bit representation");

static_assert(
    static_cast<uint8_t>(TinyStatus::STATUS_OK) == 0x00,
    "TinyStatus::STATUS_OK wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_CRC) == 0x01,
    "TinyStatus::ERR_CRC wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_TIMEOUT) == 0x02,
    "TinyStatus::ERR_TIMEOUT wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_OVERFLOW) == 0x03,
    "TinyStatus::ERR_OVERFLOW wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_BUSY) == 0x04,
    "TinyStatus::ERR_BUSY wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_PROCESSING) == 0x05,
    "TinyStatus::ERR_PROCESSING wire value changed");

static_assert(
    static_cast<uint8_t>(TinyStatus::ERR_UNKNOWN) == 0xFF,
    "TinyStatus::ERR_UNKNOWN wire value changed");
```

After separating ACK results from telemetry, retain wire assertions only for values that remain part of the ACK protocol.

### Acceptance criteria

- One-byte representation is asserted.
- Existing ACK wire values cannot change silently.
- New ACK results receive explicit numeric values.
- Removed local-only events are no longer treated as ACK wire identifiers.

---

# P1: Define the unused value range

## 8. Values `0x06` through `0xFE` have no documented policy

### Current state

The enum occupies:

```text
0x00 through 0x05
0xFF
```

The range:

```text
0x06 through 0xFE
```

is undefined.

### Required documentation

Define whether this range is:

- reserved for future TinyLink results
- available for application-defined results
- permanently invalid

### Recommendation

Reserve the undefined range for future protocol use. Do not allow application-specific meanings in the same ACK result byte unless a dedicated range is explicitly assigned.

### Acceptance criteria

- Undefined values have a documented policy.
- Existing peers reject unsupported values safely.
- Future additions do not reinterpret application-owned values.
- Protocol compatibility is considered before adding new wire results.

---

# P2: Preserve diagnostic conversion

## 9. `tinystatus_to_string()` safely handles invalid enum objects

### Current behavior

```cpp
default:
    return "INVALID";
```

This is useful for diagnostics when an invalid enum object has been created through an explicit cast.

### Required action

Retain the fallback, but keep it diagnostic-only. It must not replace raw-byte validation.

### Acceptance criteria

- Every declared value has a stable diagnostic string.
- Invalid enum objects return `INVALID`.
- Normal protocol flow rejects invalid raw bytes before callback or state transition.
- Diagnostic conversion remains allocation-free.

---

# P2: Naming and source compatibility

## 10. Current names use C-style prefixes

Current values include:

```cpp
STATUS_OK
ERR_CRC
ERR_TIMEOUT
```

Inside a scoped enum, this produces names such as:

```cpp
TinyStatus::ERR_TIMEOUT
```

A new corrected API might prefer:

```cpp
AckResult::Ok
AckResult::Busy
AckResult::Processing
AckResult::Unknown
```

This is not a correctness issue. Renaming existing members would be source-breaking.

### Recommendation

Do not perform a naming-only rewrite of `TinyStatus`.

When introducing the separated replacement types, use the repository's intended enum naming convention from the start.

---

## 11. Helper naming differs from other protocol helpers

The helper is named:

```cpp
tinystatus_to_string()
```

Other helpers include:

```cpp
message_type_to_string()
message_type_to_wire()
```

A consistent form would be:

```cpp
tiny_status_to_string()
```

This would be a source-level change and should not be mixed into correctness remediation unless the API is intentionally unstable.

---

# P2: Correct file metadata

## 12. Use source-root-relative file documentation

### Current behavior

```cpp
@file Status.h
```

The reviewed path is:

```text
src/protocol/Status.h
```

### Required change

Use:

```cpp
@file protocol/Status.h
```

### Acceptance criteria

- Public protocol headers use one consistent `@file` convention.
- Generated documentation identifies the repository location accurately.

---

# Positive findings

The following characteristics should be preserved where relevant.

## Explicit underlying type

```cpp
enum class TinyStatus : uint8_t
```

provides a stable one-byte representation.

## Explicit identifiers

Every declared value has an explicit numeric assignment.

## Distinct declared unknown

Using `0xFF` creates a deliberate upper-bound value for a valid unspecified result.

## Safe diagnostic fallback

Invalid enum objects return `INVALID` rather than falling through unpredictably.

## Allocation-free implementation

The enum and string helper are suitable for constrained embedded targets.

---

# Proposed corrected model

A minimal separation could look like this conceptually.

## Wire-level ACK result

```cpp
enum class AckResult : uint8_t {
    Ok         = 0x00,
    Busy       = 0x04,
    Processing = 0x05,
    Unknown    = 0xFF
};
```

This preserves existing bytes for outcomes that make sense as peer operation results.

## Local telemetry events

```cpp
enum class TelemetryEvent : uint8_t {
    PacketReceived,
    CobsDecodeFailed,
    ChecksumMismatch,
    PacketLengthMismatch,
    PayloadSizeMismatch,
    InterByteTimeout,
    AckTimeout,
    ReceiveOverflow,
    UnexpectedMessageType,
    UnexpectedSequence,
    InvalidHandshake
};
```

These are local identifiers and do not need to preserve the existing `TinyStatus` values.

## Optional richer application-processing results

If application handlers need additional negative acknowledgements, define them explicitly:

```cpp
enum class AckResult : uint8_t {
    Ok             = 0x00,
    Rejected       = 0x01,
    InvalidRequest = 0x02,
    Busy           = 0x04,
    Processing     = 0x05,
    Failed         = 0x06,
    Unknown        = 0xFF
};
```

Do not reuse codec failures as application-processing outcomes unless their semantics are deliberately defined.

---

# Required test suite

## Representation

- [ ] `sizeof(TinyStatus) == 1` while the type remains.
- [ ] Every declared value matches its documented byte.
- [ ] No duplicate values exist.

## Raw conversion

- [ ] `0x00` converts successfully.
- [ ] Every retained result value converts successfully.
- [ ] `0xFF` converts to declared unknown.
- [ ] `0x06` follows the selected policy.
- [ ] `0x7F` follows the selected policy.
- [ ] `0xFE` follows the selected policy.
- [ ] Failed conversion leaves output unchanged.

## Diagnostic strings

- [ ] Every declared value returns the expected string.
- [ ] Invalid cast value returns `INVALID`.
- [ ] Declared unknown returns `ERR_UNKNOWN`, not `INVALID`.

## ACK integration

- [ ] Matching ACK with `Ok` clears pending state.
- [ ] Matching ACK with `Busy` follows defined behavior.
- [ ] Matching ACK with `Processing` follows defined behavior.
- [ ] Matching ACK with `Unknown` follows defined behavior.
- [ ] Invalid raw status does not clear pending state.
- [ ] Invalid raw status does not invoke a successful callback.
- [ ] Stale ACK does not clear pending state regardless of result.

## Telemetry separation

- [ ] COBS failure increments only COBS telemetry.
- [ ] Fletcher mismatch increments only checksum telemetry.
- [ ] Inter-byte timeout increments only inter-byte telemetry.
- [ ] ACK timeout increments only ACK-timeout telemetry.
- [ ] Receive overflow increments only overflow telemetry.
- [ ] Peer `Busy` increments only peer-result telemetry if counted.
- [ ] Invalid ACK result increments invalid-status telemetry.

## Compatibility

- [ ] Existing retained ACK result bytes remain stable.
- [ ] Removed local statuses are not emitted as ACK results.
- [ ] Incompatible status models are handled through protocol-version policy where needed.

---

# Implementation checklist

## Model correction

- [ ] Separate ACK results from local telemetry events.
- [ ] Decide which current values remain valid ACK results.
- [ ] Remove CRC, timeout, and overflow from ACK results unless explicit peer semantics are defined.
- [ ] Define any additional application-processing ACK outcomes deliberately.

## Wire validation

- [ ] Add raw-to-result validation.
- [ ] Reject invalid status bytes before callback or state transition.
- [ ] Leave output unchanged on failed conversion.
- [ ] Keep declared unknown distinct from invalid input.
- [ ] Define the policy for values `0x06` through `0xFE`.

## Wire contract

- [ ] Add one-byte representation verification.
- [ ] Add fixed wire-value assertions or tests.
- [ ] Require explicit numeric values for every future ACK result.
- [ ] Consider protocol-version impact before changing retained wire values.

## Integration

- [ ] Update `TinyAck` to use the corrected `AckResult` type.
- [ ] Update `sendAck()` to accept only meaningful ACK outcomes.
- [ ] Update ACK decoding to validate raw result bytes.
- [ ] Update ACK callbacks to expose semantic results.
- [ ] Update `TinyStats` to use local telemetry events.
- [ ] Split COBS, checksum, length, overflow, and timeout counters.

## Documentation and naming

- [ ] Correct `@file` to `protocol/Status.h`.
- [ ] Document retained ACK result semantics.
- [ ] Document reserved value ranges.
- [ ] Decide naming conventions only after separating the model.
- [ ] Keep diagnostic string conversion allocation-free.

## Verification

- [ ] Add representation tests.
- [ ] Add raw conversion tests.
- [ ] Add ACK behavior tests.
- [ ] Add telemetry mapping tests.
- [ ] Add compatibility tests.

---

# Definition of done

The status-model remediation is complete when:

- [ ] ACK results describe only outcomes for an identified application frame.
- [ ] Local codec, parser, timeout, and overflow events use a separate telemetry type.
- [ ] Undefined raw bytes cannot enter callbacks or clear pending ACK state.
- [ ] Declared unknown result remains distinct from invalid input.
- [ ] Every retained wire value is explicit and protected.
- [ ] Undefined value ranges have a documented policy.
- [ ] `TinyAck`, `TinyStats`, and `TinyLink` use the corrected types consistently.
- [ ] Representation, validation, ACK behavior, telemetry mapping, and compatibility tests pass.

---

# Verdict

`Status.h` is mechanically correct as a one-byte enum with explicit values. Its problem is responsibility, not representation.

`TinyStatus` currently combines:

```text
peer ACK outcome
```

with:

```text
local codec and protocol telemetry
```

That conflation propagates into ACK handling, statistics, timeout reporting, and error naming.

The durable correction is to split the model into a wire-level ACK result and local telemetry events. Only after that split should `TinyAck`, `TinyStats`, and related callbacks be finalized.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/Version.h`
