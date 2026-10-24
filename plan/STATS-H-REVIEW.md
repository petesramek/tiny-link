# Review of `src/protocol/Stats.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Stats.h`

`Stats.h` is generally straightforward, bounded, and suitable for an embedded library. It has one definite pointer-safety defect in `to_string()` and several telemetry-model weaknesses inherited from the current use of `TinyStatus` throughout `TinyLink.h`.

---

# Summary

## Confirmed defect

1. `to_string()` does not validate `buf` before passing it to `snprintf()`.

## Important design issues

1. Counters can overflow silently.
2. `merge()` can overflow silently.
3. Formatting assumes that converting `uint32_t` to `unsigned` preserves the complete value.
4. The formatting result hides whether output was truncated.
5. Invalid `TinyStatus` values are merged into the legitimate `ERR_UNKNOWN` counter.
6. Local engine events and peer-reported ACK results share the same status model.
7. `crc` combines COBS, checksum, length, and other decode failures.
8. `timeout` combines inter-byte and ACK timeouts.
9. Stats access is unsafe if `update()` runs in an ISR while counters are read elsewhere.
10. Returning a live reference can expose inconsistent snapshots.
11. Public mutable fields make telemetry invariants difficult to preserve.

---

# P0: Validate the formatting output buffer

## 1. `to_string()` accepts a null destination with positive capacity

### Current behavior

```cpp
inline int to_string(char* buf, int bufSize) const {
    if (bufSize <= 0) return 0;

    int n = snprintf(
        buf,
        static_cast<size_t>(bufSize),
        // ...
    );
}
```

The method validates only the numeric capacity.

This call passes the initial check:

```cpp
stats.to_string(nullptr, 64);
```

It then passes a null destination and positive size to `snprintf()`, which is invalid.

### Required change

```cpp
if (!buf || bufSize <= 0) {
    return 0;
}
```

A cleaner API would use `size_t` for capacity:

```cpp
int toString(char* buffer, size_t capacity) const;
```

That removes the negative-size case entirely.

### Acceptance criteria

- Null buffer returns failure without calling `snprintf()`.
- Zero capacity returns failure.
- Positive capacity with a valid buffer reaches formatting.
- No invalid pointer and capacity combination reaches the C library.

### Required tests

- Null buffer with zero capacity.
- Null buffer with positive capacity.
- Valid buffer with zero capacity.
- Valid buffer with capacity one.
- Valid buffer with exact capacity.
- Valid buffer with larger capacity.

---

# P1: Preserve truncation information

## 2. `to_string()` hides whether output was truncated

### Current behavior

```cpp
if (n < 0) return 0;
return (n >= bufSize) ? (bufSize - 1) : n;
```

`snprintf()` normally returns the number of characters that would have been written if enough capacity were available.

The current implementation replaces that value with the number of characters actually stored when truncation occurs.

This matches the current comment:

> Number of characters written, excluding NUL.

However, the caller cannot distinguish between:

- complete output whose length is `bufSize - 1`
- truncated output that filled `bufSize - 1` bytes

### Required decision

Choose one contract.

## Option A: Preserve `snprintf()` semantics

Return `n` directly:

```cpp
return n;
```

The caller detects truncation using:

```cpp
n >= static_cast<int>(capacity)
```

## Option B: Return a typed result

```cpp
struct FormatResult {
    bool success;
    bool truncated;
    size_t written;
    size_t required;
};
```

### Recommendation

For a small embedded API, preserve conventional `snprintf()` semantics. This provides truncation detection without introducing another public type.

### Acceptance criteria

- Callers can detect truncation.
- Return semantics are documented precisely.
- Formatting failure is distinguishable from valid empty output.
- A one-byte destination remains terminated.

### Required tests

- Buffer much smaller than required.
- Buffer exactly one byte too small.
- Exact required capacity including terminator.
- Larger buffer.
- Verify both written text and returned required length.

---

# P1: Format fixed-width counters portably

## 3. `%u` relies on `unsigned` preserving all `uint32_t` values

### Current behavior

The counter type is:

```cpp
typedef uint32_t CountType;
```

Formatting converts counters to `unsigned`:

```cpp
static_cast<unsigned>(packets)
```

and prints them with:

```cpp
%u
```

This is correct on common TinyLink targets where `unsigned` is at least 32 bits. It is not the strongest portable contract because `uint32_t` is not guaranteed to be an alias of `unsigned int` on every implementation.

### Required change

Use the standard fixed-width formatting macros:

```cpp
#include <inttypes.h>
```

Then format with `PRIu32`:

```cpp
"ok=%" PRIu32 " crc=%" PRIu32
```

Alternatively, document and verify that every supported target has a 32-bit `unsigned`.

### Acceptance criteria

- `UINT32_MAX` formats without truncation.
- Format specifiers match argument types exactly.
- Formatting works on every supported compiler and target.
- No narrowing conversion is required before formatting.

### Required tests

- All counters zero.
- Counter value one.
- Counter value `UINT32_MAX`.
- Mixed minimum and maximum values.
- Same expected text on AVR and desktop targets.

---

# P1: Define counter overflow behavior

## 4. `increment()` wraps silently

### Current behavior

```cpp
++packets;
```

Because counters are `uint32_t`, incrementing `UINT32_MAX` wraps to zero.

On a long-running device, a very high error count can therefore suddenly look like no errors occurred.

### Required decision

Choose one policy.

## Wrapping counters

Document modulo-`2^32` behavior.

## Saturating counters

```cpp
if (counter != UINT32_MAX) {
    ++counter;
}
```

### Recommendation

Use saturating counters for diagnostics. Saturation at the maximum still communicates that the event occurred very frequently, while wrapping to zero can be actively misleading.

### Acceptance criteria

- Overflow behavior is documented.
- Incrementing a maximum counter follows the selected policy.
- Counters never appear reset unless wrapping is intentionally selected.
- Every counter uses the same default overflow policy.

### Required tests

- Increment zero.
- Increment `UINT32_MAX - 1`.
- Increment `UINT32_MAX`.
- Multiple increments after saturation if saturation is selected.

---

## 5. `merge()` can overflow independently

### Current behavior

```cpp
packets += o.packets;
```

Every merged field can wrap.

If `increment()` is changed to saturation but `merge()` retains wrapping addition, the telemetry model becomes inconsistent.

### Required change

Apply the same overflow policy to both increment and merge.

For saturation:

```cpp
inline CountType saturating_add(
    CountType left,
    CountType right)
{
    return right > UINT32_MAX - left
        ? UINT32_MAX
        : left + right;
}
```

Use it for every merged counter.

### Acceptance criteria

- `increment()` and `merge()` share one overflow policy.
- Merging near-maximum values cannot unexpectedly produce a small result.
- Self-merge behavior is defined.
- Every field follows the same addition policy unless explicitly documented otherwise.

### Required tests

- Merge zero into zero.
- Merge ordinary values.
- Merge values summing exactly to `UINT32_MAX`.
- Merge values exceeding `UINT32_MAX`.
- Self-merge small values.
- Self-merge near-maximum values.

---

# P1: Separate local telemetry from ACK result statuses

## 6. `TinyStatus` currently serves two different domains

### Current counters

Some fields describe local engine behavior:

```cpp
packets
overflow
crc
timeout
```

Other fields correspond to peer-reported ACK results:

```cpp
busy
processing
```

These represent different categories:

- local codec failures
- local parser events
- local transport events
- successful local receipt
- results reported by a remote peer

### Example ambiguity

`ERR_BUSY` could mean:

- a remote peer rejected a request because it was busy
- the local engine was busy
- a received ACK contained a busy result

The counter does not say which event occurred.

### Required change

Separate local telemetry events from wire-level ACK outcomes.

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

Peer outcomes can have separate storage:

```cpp
struct AckResultStats {
    CountType ok;
    CountType busy;
    CountType processing;
    CountType unknown;
};
```

### Acceptance criteria

- Every counter has one precise meaning.
- Local failures are not represented as peer ACK statuses.
- Peer outcomes are distinguishable from local engine failures.
- Counter names identify the event without requiring hidden context.
- Stats formatting groups local events and peer outcomes clearly.

---

# P1: Split overly broad counters

## 7. `crc` combines unrelated failure paths

### Current documentation

```cpp
crc ///< Fletcher-16 / COBS framing failures
```

COBS framing and Fletcher checksum failures are not the same event.

The current APIs can also collapse additional conditions into the same status:

- malformed COBS data
- COBS output buffer too small
- packet length mismatch
- checksum mismatch
- payload output too small
- local packet packing failure
- local COBS encoding failure

### Required change

Introduce precise counters, such as:

```cpp
cobsDecodeFailures
checksumFailures
packetLengthFailures
payloadSizeFailures
encodeFailures
```

At minimum, if one combined counter is intentionally retained, rename it to something broader than `crc`.

### Recommendation

Split these counters after the codec APIs return typed results. Each codec result can map directly to one telemetry event.

### Acceptance criteria

- COBS failure is not reported as CRC failure.
- Length mismatch is distinguishable from checksum mismatch.
- Output-capacity failure is distinguishable from malformed input.
- Local encode failure is not counted as remote data corruption.
- Tests assert the exact counter for every failure path.

---

## 8. `timeout` combines different deadlines

### Current documentation

```cpp
timeout ///< Inter-byte timeouts
```

### Actual use in `TinyLink.h`

`ERR_TIMEOUT` is incremented for:

- a stalled partial receive frame
- a missing application ACK

These events have different causes and different operational implications.

### Required change

Separate at least:

```cpp
interByteTimeouts
ackTimeouts
```

If handshake retries become bounded, handshake failure or handshake timeout should also have a distinct event.

### Acceptance criteria

- RX timeout and ACK timeout are independently observable.
- Handshake timeout or retry exhaustion has its own category if introduced.
- Every timeout path increments exactly one intended counter.
- Formatting labels identify each timeout category clearly.

---

# P1: Distinguish declared unknown status from invalid input

## 9. Invalid enum values are merged into `ERR_UNKNOWN`

### Current behavior

```cpp
case TinyStatus::ERR_UNKNOWN:
    ++unknown;
    break;

default:
    ++unknown;
    break;
```

This combines:

- a legitimate declared `ERR_UNKNOWN` result
- an invalid cast such as `static_cast<TinyStatus>(0xFF)`
- future status values not recognized by this build

These are not necessarily equivalent.

### Required change

If unsupported raw values are rejected, track them separately:

```cpp
invalidStatusValues
```

If future values are preserved for compatibility, record the raw value through a separate result mechanism.

### Acceptance criteria

- Declared `ERR_UNKNOWN` remains distinguishable from malformed status input.
- Future values follow a documented compatibility policy.
- Invalid values cannot silently appear as legitimate peer-reported unknown errors.
- Tests cover both declared unknown and invalid raw values.

---

# P1: Make stats safe in the selected execution model

## 10. Multi-byte counters are unsafe across ISR and foreground contexts

### Current behavior

`TinyLink.h` advertises `autoUpdateISR()`, which can call protocol processing and increment counters:

```cpp
_stats.increment(...)
```

Foreground code can simultaneously obtain:

```cpp
const TinyStats& getStats() const;
```

On an 8-bit AVR, 32-bit reads and writes are not atomic. A foreground read can observe a torn value while an ISR increments it.

`clear()` and `merge()` are also unsafe during concurrent updates.

### Required change

The preferred correction remains removing full protocol processing from ISR context.

If counters are genuinely shared across ISR and foreground contexts, provide one of:

- critical sections
- atomic snapshot operation
- task-context-only access rules
- platform-specific synchronization abstraction

Do not rely on `volatile`. It does not make a multi-byte operation atomic.

### Acceptance criteria

- Stats are updated and read in one documented execution context, or access is synchronized.
- Reading a counter cannot observe a torn 32-bit value.
- `clear()` cannot race with increments.
- `merge()` cannot race with increments.
- ISR documentation matches actual safety guarantees.

### Required tests

- Repeated snapshot while counters are updated.
- Clear while update activity is simulated.
- AVR-specific atomicity test or protected snapshot test.
- Thread-sanitizer coverage on desktop where the execution model permits it.

---

# P1: Return a consistent snapshot

## 11. `getStats()` exposes a live reference

### Current behavior

`TinyLink.h` returns:

```cpp
const TinyStats& getStats() const
```

The caller cannot modify through the reference, but the object can change while it is being read if protocol processing continues.

A caller reading multiple fields can observe a combination that never existed at one moment.

### Required change

Return a snapshot by value:

```cpp
TinyStats stats() const;
```

In a single-threaded polling model, normal copying is sufficient.

In a concurrent or ISR model, copy under the selected synchronization mechanism.

### Acceptance criteria

- Callers receive a consistent snapshot.
- Snapshot semantics are documented.
- Reading multiple counters cannot produce an arbitrary mixture of update moments.
- Snapshot copying remains acceptable for constrained targets.

---

# P2: Decide whether fields are live state or snapshot data

## 12. Public mutable fields bypass invariants

### Current behavior

Every field is public:

```cpp
stats.crc = 0;
stats.timeout = 900;
```

This bypasses:

- saturation rules
- concurrency protection
- validation
- future event mapping

### Required decision

Choose one role for the public type.

## Plain snapshot value

Public fields are acceptable because the object is a detached copy.

## Managed live counter storage

Keep counters private and expose read-only accessors.

### Recommendation

Use two responsibilities:

- private mutable counter storage inside `TinyLink`
- public copyable snapshot returned by value

The snapshot can use public fields if desired. The live internal object should not be exposed by reference.

### Acceptance criteria

- The public type's role is documented.
- Live mutation cannot bypass internal overflow or synchronization rules.
- Snapshot consumers can read values conveniently.
- Internal state remains encapsulated.

---

# P2: Maintainability improvements

## 13. Expand chained clearing assignments

### Current behavior

```cpp
packets = crc = timeout = overflow =
    busy = processing = unknown = 0;
```

This is correct because every field currently uses the same type.

Explicit assignment is more robust if fields later gain different types or reset behavior:

```cpp
packets = 0;
crc = 0;
timeout = 0;
overflow = 0;
busy = 0;
processing = 0;
unknown = 0;
```

This is a maintainability preference, not a correctness defect.

---

## 14. Existing initialization is correct

The constructor explicitly initializes every field to zero:

```cpp
TinyStats()
    : packets(0),
      crc(0),
      timeout(0),
      overflow(0),
      busy(0),
      processing(0),
      unknown(0)
{}
```

This is correct.

In-class initializers or aggregate value initialization may be considered depending on the supported C++ baseline, but no change is required for correctness.

---

## 15. Use event-oriented names after correcting the model

Current names are brief:

```cpp
crc
timeout
unknown
```

They do not fully explain what was counted.

Prefer event-oriented names after the telemetry categories are corrected:

```cpp
packetsReceived
checksumFailures
interByteTimeouts
receiveOverflows
```

Do not perform naming-only cleanup before deciding the actual event model.

---

# P2: Correct file metadata

## 16. Use source-root-relative `@file` documentation

### Current behavior

```cpp
@file Stats.h
```

The reviewed path is:

```text
src/protocol/Stats.h
```

### Required change

Use:

```cpp
@file protocol/Stats.h
```

This matches the convention already used by `protocol/LogLevel.h`.

### Acceptance criteria

- Public protocol headers use one consistent `@file` convention.
- Generated documentation identifies the repository location accurately.

---

# Positive findings

The following characteristics should be retained.

## Fixed-width counters

```cpp
typedef uint32_t CountType;
```

provides a consistent counter width across supported platforms.

## Explicit initialization

Every counter starts at zero.

## Defensive status switch

The switch handles every currently referenced status and includes a fallback.

## Bounded formatting

`snprintf()` is used instead of unbounded `sprintf()`.

## Allocation-free design

The type uses no heap or STL facilities.

## Merge support

Combining snapshots is useful for tests, multiple links, or periodic telemetry once overflow behavior is defined.

---

# Required test suite

## Initialization and clearing

- [ ] New stats contain all zeros.
- [ ] Every nonzero field returns to zero after `clear()`.
- [ ] Repeated clearing remains stable.

## Increment mapping

- [ ] Every declared input updates exactly one expected counter.
- [ ] No unrelated counter changes.
- [ ] Invalid enum input follows the selected policy.
- [ ] Declared `ERR_UNKNOWN` is distinguishable from invalid status if the model is corrected.

## Overflow behavior

- [ ] Increment zero.
- [ ] Increment `UINT32_MAX - 1`.
- [ ] Increment `UINT32_MAX`.
- [ ] Merge values that fit.
- [ ] Merge values that sum exactly to `UINT32_MAX`.
- [ ] Merge values that exceed `UINT32_MAX`.
- [ ] Self-merge behavior.
- [ ] Increment and merge use the same overflow policy.

## Formatting

- [ ] Null buffer with positive capacity.
- [ ] Null buffer with zero capacity.
- [ ] Valid buffer with zero capacity.
- [ ] Capacity one.
- [ ] Small truncated buffer.
- [ ] Exact capacity.
- [ ] Larger buffer.
- [ ] All counters zero.
- [ ] Maximum counter values.
- [ ] Truncation is detectable.
- [ ] Output is NUL-terminated whenever capacity is positive.
- [ ] Return value follows the documented semantics.

## Telemetry mapping

- [ ] COBS decode failure increments only its intended event.
- [ ] Fletcher mismatch increments only checksum failure.
- [ ] Packet length mismatch increments only length failure.
- [ ] Payload size mismatch increments only payload failure.
- [ ] Local encode failure is not counted as remote corruption.
- [ ] Inter-byte timeout does not increment ACK timeout.
- [ ] ACK timeout does not increment inter-byte timeout.
- [ ] Overflow increments once per oversized frame.
- [ ] Peer busy result is separate from local engine state.
- [ ] Invalid raw status is separate from declared unknown result.

## Snapshot and concurrency

If concurrent or ISR updates remain:

- [ ] Snapshot while an increment occurs.
- [ ] Clear while an increment occurs.
- [ ] Merge is prohibited concurrently or protected.
- [ ] No torn 32-bit values on AVR.
- [ ] Snapshot fields come from one consistent observation.

---

# Implementation checklist

## Immediate formatting fix

- [ ] Validate `buf` before calling `snprintf()`.
- [ ] Change capacity from `int` to `size_t` if compatibility permits.
- [ ] Preserve or explicitly report truncation.
- [ ] Use portable `uint32_t` formatting.

## Overflow behavior

- [ ] Choose wrapping or saturating counters.
- [ ] Document the policy.
- [ ] Apply it to `increment()`.
- [ ] Apply it to `merge()`.
- [ ] Test boundary values.

## Telemetry model

- [ ] Separate local events from peer ACK outcomes.
- [ ] Split COBS and checksum failures.
- [ ] Split packet length and payload size failures.
- [ ] Split inter-byte and ACK timeouts.
- [ ] Add handshake timeout or failure event if needed.
- [ ] Distinguish invalid status from declared unknown status.
- [ ] Rename counters after meanings are finalized.

## Execution safety

- [ ] Remove protocol processing from ISR context, or synchronize stats access.
- [ ] Return a snapshot by value.
- [ ] Define snapshot consistency rules.
- [ ] Prevent `clear()` and `merge()` races.
- [ ] Do not rely on `volatile` for multi-byte atomicity.

## Public API

- [ ] Decide whether public fields describe detached snapshots.
- [ ] Keep live mutable storage private.
- [ ] Preserve convenient read access for consumers.
- [ ] Update formatting API documentation.

## Documentation

- [ ] Correct `@file` to `protocol/Stats.h`.
- [ ] Document every counter's exact event meaning.
- [ ] Document overflow behavior.
- [ ] Document formatting return and truncation semantics.
- [ ] Review `src/protocol/Status.h` before finalizing event categories.

---

# Definition of done

The `Stats.h` remediation is complete when:

- [ ] Null formatting buffers cannot reach `snprintf()`.
- [ ] Callers can detect truncated output.
- [ ] Full 32-bit values format portably.
- [ ] Counter overflow follows a documented policy.
- [ ] Increment and merge share the same overflow behavior.
- [ ] Every counter represents one precise event.
- [ ] Local engine failures and peer ACK outcomes are separate.
- [ ] COBS, checksum, length, and timeout events are distinguishable.
- [ ] Invalid raw statuses are distinct from declared unknown results.
- [ ] Stats snapshots are consistent and safe in the selected execution model.
- [ ] Formatting, overflow, mapping, snapshot, and concurrency tests pass.

---

# Verdict

`Stats.h` is straightforward, bounded, and suitable for an embedded library in its basic structure.

The immediate local defect is:

```cpp
to_string(nullptr, positiveSize)
```

which can pass a null destination to `snprintf()`.

The larger problem is telemetry accuracy. `TinyStats` currently mirrors the overloaded `TinyStatus` model, causing codec failures, parser events, timeouts, and peer-reported ACK results to share categories that do not describe what actually happened.

Before expanding the counters, review the status model and decide whether local telemetry events and wire ACK outcomes need separate enums.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`
