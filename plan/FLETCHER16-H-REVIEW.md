# Review of `src/internal/codec/Fletcher16.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/codec/Fletcher16.h`

The implementation is compact, uses constant memory, and is suitable for constrained embedded targets. No out-of-bounds access or accumulator overflow is apparent for valid TinyLink inputs. The principal concern is algorithm compatibility, especially the final accumulator reduction.

---

# Summary

## Main findings

1. The final Fletcher accumulator reduction appears incomplete and must be verified against independent golden vectors.
2. A null data pointer with nonzero length causes undefined behavior.
3. Empty input returns `0xFFFF`, which is valid only if this is the explicitly selected TinyLink checksum variant.
4. The exact Fletcher-16 variant needs to be documented through fixed protocol vectors.
5. The 20-byte reduction interval should be named or explained.

## Release impact

The apparent accumulator-reduction problem may change checksum values for some frames. Because the same implementation is used for both packing and unpacking, ordinary TinyLink round-trip tests may not reveal the problem.

Any correction must therefore be treated as a possible wire-protocol compatibility change.

---

# P0: Verify and correct final accumulator reduction

## 1. Final reduction appears incomplete

### Current behavior

The implementation performs one fold after every block:

```cpp
sum1 = (sum1 & 0xff) + (sum1 >> 8);
sum2 = (sum2 & 0xff) + (sum2 >> 8);
```

It then assembles the checksum directly:

```cpp
return (sum2 << 8) | sum1;
```

After one fold, an accumulator can still be `0x100`. For example, a low byte of `0xFF` plus a high byte of `0x01` produces `0x100`.

If `sum1` retains that ninth bit, this expression:

```cpp
(sum2 << 8) | sum1
```

allows the remaining bit from `sum1` to affect the high checksum byte. This mixes the accumulator fields rather than returning an eight-bit `sum2` followed by an eight-bit `sum1`.

### Required investigation

Before changing the implementation:

1. Generate vectors from the current implementation.
2. Compare them with an independently implemented and verified Fletcher-16 variant using the intended initial values.
3. Identify inputs for which the current and reference implementations differ.
4. Determine whether released TinyLink peers depend on the current output.
5. Decide whether correction requires a protocol-version change.

Do not validate this only through `Packet::pack()` followed by `Packet::unpack()`. Both operations use the same checksum implementation and can agree on an incorrect or nonstandard result.

### Likely correction

If independent verification confirms the issue, perform a complete final fold before assembling the checksum:

```cpp
sum1 = (sum1 & 0xff) + (sum1 >> 8);
sum2 = (sum2 & 0xff) + (sum2 >> 8);

sum1 = (sum1 & 0xff) + (sum1 >> 8);
sum2 = (sum2 & 0xff) + (sum2 >> 8);

return static_cast<uint16_t>((sum2 << 8) | sum1);
```

An explicit modulo-255 implementation is another option:

```cpp
sum1 %= 255;
sum2 %= 255;
```

The folded implementation is more consistent with the current embedded-oriented design.

### Acceptance criteria

- The selected Fletcher-16 variant is defined precisely.
- All returned accumulator fields are reduced to eight bits before assembly.
- The implementation matches independent golden vectors.
- AVR and non-AVR targets produce identical results.
- Compatibility impact is assessed before changing released wire behavior.

### Required tests

- Inputs that cause a first fold to produce `0x100`.
- Inputs where the current implementation and fully reduced implementation differ.
- Multiple block-boundary lengths.
- Actual TinyLink packet frames.
- Maximum TinyLink plain-frame length.
- Independent-reference comparison.

---

# P0: Define pointer validity requirements

## 2. Null pointer with nonzero length causes undefined behavior

### Current behavior

The processing loop dereferences `data` without checking it:

```cpp
sum1 += *data++;
```

The call is invalid when:

```cpp
data == nullptr && len > 0
```

The normal `Packet.h` call path currently provides a valid packet buffer. However, `fletcher16()` is still a callable helper and its argument contract must be explicit.

### Required decision

Choose one model.

#### Preferred internal-helper contract

Document this precondition:

> When `len` is nonzero, `data` must point to at least `len` readable bytes.

Validate pointers at the packet boundary before calling the checksum function.

A debug assertion can be added on supported platforms if it does not compromise AVR compatibility.

#### Defensive status-returning API

A status-returning checksum API could report invalid input explicitly. Returning a sentinel checksum such as zero is insufficient because every 16-bit checksum value may be valid.

### Recommendation

Keep the small internal function and document the precondition. Avoid returning a misleading sentinel value for invalid input.

### Acceptance criteria

- The non-null precondition for nonzero length is documented.
- Every caller satisfies or validates the precondition.
- Null pointer with zero length has a defined outcome.
- Null pointer with nonzero length is covered by assertions or invalid-argument tests according to the selected contract.

---

# P1: Define the exact checksum variant

## 3. Empty input returns `0xFFFF`

### Current behavior

The accumulators start at `0xFF`:

```cpp
uint16_t sum1 = 0xff;
uint16_t sum2 = 0xff;
```

When `len == 0`, no loop iteration occurs and the result is:

```text
0xFFFF
```

This is not necessarily a defect. It is a protocol-specific algorithm choice that must be documented because Fletcher-16 implementations can use different initial accumulator values and finalization rules.

### Required action

Document the selected algorithm parameters explicitly:

- initial `sum1`
- initial `sum2`
- reduction modulus
- reduction interval
- final reduction steps
- returned accumulator order
- packet byte order
- empty-input checksum

Suggested specification:

```text
Initial sum1: 0xFF
Initial sum2: 0xFF
Reduction modulus: 255
Reduction interval: 20 bytes
Result value: high byte sum2, low byte sum1
Packet storage: low checksum byte first
Empty-input result: 0xFFFF
```

The specification must reflect the final verified algorithm rather than merely describing the current implementation.

### Acceptance criteria

- Empty-input behavior is intentional and documented.
- Tests assert the exact empty-input checksum.
- Packet documentation differentiates checksum numeric value from its stored byte order.

---

## 4. Identify the Fletcher-16 variant through golden vectors

### Problem

The function name `fletcher16()` alone is not enough to define interoperability. Implementations may differ in:

- initial accumulator values
- reduction strategy
- final reduction
- empty-input handling
- result-byte order
- packet-storage byte order

### Required change

Add fixed golden vectors to the protocol test suite and, where appropriate, the protocol documentation.

Expected values must be independently calculated. Tests must not derive their expected checksums by calling the implementation under test.

Example test-vector shape:

```cpp
struct FletcherVector {
    const uint8_t* data;
    size_t length;
    uint16_t expected;
};
```

### Acceptance criteria

- Golden vectors exist for trivial, boundary, and real packet inputs.
- Expected values come from an independent implementation or manual reference calculation.
- The same vectors run on every supported platform.
- Packet tests separately verify checksum value and checksum storage order.

---

# P2: Maintainability improvements

## 5. Name or explain the 20-byte reduction interval

### Current behavior

The function uses a literal block size:

```cpp
size_t tlen = len > 20 ? 20 : len;
```

The value is part of the arithmetic-safety strategy. It is not an arbitrary performance tuning value.

### Required change

Use a named constant:

```cpp
static const size_t REDUCTION_BLOCK_SIZE = 20;
```

Add a concise explanation that the interval keeps the intermediate `uint16_t` accumulators within range.

### Acceptance criteria

- The purpose of the block size is documented.
- Future changes cannot casually increase it without reviewing accumulator bounds.
- Boundary tests cover lengths immediately below, at, and above the block size.

---

## 6. Make result narrowing explicit

### Current behavior

The checksum expression is implicitly narrowed to `uint16_t` by the return type:

```cpp
return (sum2 << 8) | sum1;
```

### Required change

After complete reduction is guaranteed, make the intended result conversion explicit:

```cpp
return static_cast<uint16_t>((sum2 << 8) | sum1);
```

This is a readability and compiler-diagnostic improvement. It does not replace the need for correct final reduction.

### Acceptance criteria

- Both accumulators are fully reduced before assembly.
- The final narrowing is explicit.
- No checksum correctness relies on implicit truncation.

---

# Arithmetic safety assessment

The 20-byte reduction interval keeps intermediate values within `uint16_t` for the current algorithm.

Each byte increases `sum1` by at most 255. `sum2` accumulates the evolving `sum1`. Across a 20-byte block, the resulting values remain below 65,535 when starting from reduced accumulator values.

The current byte input type is also correct:

```cpp
const uint8_t* data
```

This prevents values above `0x7F` from being interpreted as negative signed characters.

No accumulator overflow or signed-byte defect is apparent for supported TinyLink input lengths.

---

# Positive findings

The following characteristics should be preserved.

## Embedded suitability

- No dynamic allocation.
- No STL dependency.
- Constant additional memory.
- Linear processing time.
- Small implementation footprint.

## Byte handling

Using `uint8_t` gives deterministic unsigned byte values across AVR and desktop targets.

## Bounded arithmetic blocks

Periodic reduction prevents unbounded accumulator growth and is appropriate for 16-bit embedded arithmetic.

## Safe length decrement

The selected block length never exceeds the remaining input length, so `len -= tlen` does not underflow.

---

# Required test suite

## Independent golden vectors

- [ ] Empty input.
- [ ] One `0x00` byte.
- [ ] One `0x01` byte.
- [ ] One `0xFF` byte.
- [ ] ASCII `123456789`.
- [ ] Repeated zero bytes.
- [ ] Repeated `0xFF` bytes.
- [ ] Sequential bytes from `0x00` through `0xFF`.
- [ ] Actual TinyLink packet headers.
- [ ] Actual TinyLink headers and payloads.
- [ ] Maximum TinyLink plain packet.

## Reduction boundaries

Test exact lengths:

- [ ] 0
- [ ] 1
- [ ] 19
- [ ] 20
- [ ] 21
- [ ] 39
- [ ] 40
- [ ] 41
- [ ] 69

The 69-byte case corresponds to the current maximum TinyLink plain-frame size.

## Final-fold regression vectors

- [ ] Input producing `sum1 == 0x100` after the first fold.
- [ ] Input producing `sum2 == 0x100` after the first fold.
- [ ] Input where the current implementation differs from complete modulo-255 reduction.
- [ ] Verify both checksum bytes independently.

## Pointer contract

- [ ] Null pointer with zero length follows the documented contract.
- [ ] Null pointer with nonzero length triggers the selected assertion or invalid-input behavior.

## Cross-platform consistency

Run exactly the same vectors on:

- [ ] AVR
- [ ] ARM
- [ ] ESP32
- [ ] x86
- [ ] x64

All platforms must return identical values.

## Packet integration

- [ ] `Packet::pack()` stores the low checksum byte first.
- [ ] `Packet::unpack()` reconstructs the same numeric checksum.
- [ ] Corrupting either stored checksum byte causes packet rejection.
- [ ] Packet tests use fixed expected bytes rather than values generated dynamically by `fletcher16()`.

---

# Implementation checklist

## Algorithm verification

- [ ] Define the intended TinyLink Fletcher-16 variant.
- [ ] Compare the implementation with an independent reference.
- [ ] Find vectors that exercise the remaining ninth accumulator bit.
- [ ] Confirm whether the final fold is missing.
- [ ] Correct the final reduction if confirmed.

## Compatibility handling

- [ ] Compare old and corrected outputs over all relevant packet lengths.
- [ ] Determine whether any released peer relies on current checksum behavior.
- [ ] Decide whether a wire-protocol version change is required.
- [ ] Document any compatibility break and migration path.

## Contract and documentation

- [ ] Document the pointer precondition.
- [ ] Define null pointer with zero-length behavior.
- [ ] Document initial accumulator values.
- [ ] Document reduction modulus and interval.
- [ ] Document empty-input result.
- [ ] Document numeric result order and packet byte order.

## Maintainability

- [ ] Replace literal `20` with a named constant or justified comment.
- [ ] Add an explicit `uint16_t` cast after complete reduction.
- [ ] Keep the implementation free of unnecessary dynamic or STL dependencies.

## Verification

- [ ] Add independent golden vectors.
- [ ] Add boundary tests around every 20-byte reduction point.
- [ ] Add final-fold regression vectors.
- [ ] Add maximum TinyLink frame test.
- [ ] Run vectors on AVR and non-AVR targets.
- [ ] Add packet-level checksum-byte-order tests.

---

# Definition of done

The `Fletcher16.h` remediation is complete when:

- [ ] The exact TinyLink Fletcher-16 variant is documented.
- [ ] Independent golden vectors confirm the implementation.
- [ ] Both accumulators are fully reduced before checksum assembly.
- [ ] No result relies on implicit truncation of residual accumulator bits.
- [ ] Empty-input behavior is explicit and tested.
- [ ] Invalid pointer usage has a documented contract.
- [ ] The 20-byte reduction interval is named or justified.
- [ ] AVR and desktop builds produce identical checksums.
- [ ] Packet storage byte order is separately tested.
- [ ] Compatibility impact has been assessed before any changed checksum is released.

---

# Verdict

`Fletcher16.h` is small, bounded, and suitable for embedded execution. Its main risk is not ordinary memory safety in the current TinyLink call path. The important risk is checksum correctness and wire compatibility.

The apparent incomplete final reduction must be verified with independent vectors. If correcting it changes existing checksum values, the change must be handled as a protocol compatibility decision rather than a routine internal refactor.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/codec/CobsCodec.h`

Every received frame passes through COBS decoding before packet validation, making that file the next memory-safety and correctness priority.
