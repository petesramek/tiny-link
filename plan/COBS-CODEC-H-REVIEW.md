# Review of `src/internal/codec/CobsCodec.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/codec/CobsCodec.h`

The core COBS encode and decode logic appears correct for TinyLink's current frame sizes. No obvious out-of-bounds write is present when valid pointers and accurate capacities are supplied. The main problems concern pointer validation, ambiguous return values, partial output on failure, and duplicated encoded-size knowledge.

---

# Summary

## Confirmed issues

1. `cobs_encode()` can dereference null source or destination pointers.
2. `cobs_decode()` can dereference null source or destination pointers.
3. A zero decode result represents both valid empty output and multiple failure conditions.
4. Failed encoding and decoding can leave partially modified output buffers.
5. The decoder's addition-based boundary check should be replaced with a subtraction-based check.
6. Required encoded-capacity calculation is not centralized or overflow-safe.
7. Decoder overlap behavior is not documented.

## Positive assessment

The central encoding and decoding loops correctly implement the expected COBS block behavior for realistic TinyLink inputs. Unlike the reviewed Fletcher-16 implementation, no obvious wire-algorithm defect is visible here.

---

# P0: Pointer validation

## 1. Validate pointers in `cobs_encode()`

### Current behavior

The only initial check is:

```cpp
if (dstCap == 0) {
    return 0;
}
```

The function does not validate `dst`. Even empty input eventually writes the final code byte:

```cpp
dst[code_idx] = code;
```

The function also reads from `src` whenever `srcLen > 0`:

```cpp
if (src[read_idx] == 0) {
```

These calls therefore cause undefined behavior:

```cpp
cobs_encode(src, srcLen, nullptr, 10);
cobs_encode(nullptr, 1, dst, dstCap);
```

### Required change

Validate arguments before initializing or writing output:

```cpp
if (!dst || dstCap == 0) {
    return 0;
}

if (!src && srcLen > 0) {
    return 0;
}
```

A null source with zero length can be accepted because no source byte needs to be read. That policy must be documented and tested.

### Acceptance criteria

- Null destination always fails safely.
- Null source with nonzero length always fails safely.
- Null source with zero length follows the documented empty-input policy.
- No invalid pointer combination causes memory access.

### Required tests

- Null destination with positive capacity.
- Null destination with zero capacity.
- Null source with zero length.
- Null source with nonzero length.
- Valid source and destination with exact capacity.

---

## 2. Validate pointers in `cobs_decode()`

### Current behavior

The decoder does not validate arguments before reading:

```cpp
uint8_t code = src[read_idx++];
```

or writing:

```cpp
dst[write_idx++] = src[read_idx++];
```

The following calls can cause undefined behavior:

```cpp
cobs_decode(nullptr, 1, dst, dstCap);
cobs_decode(src, srcLen, nullptr, positiveCapacity);
```

### Required change

Define and enforce a clear pointer contract.

At minimum:

```cpp
if (!src && srcLen > 0) {
    return 0;
}

if (!dst && dstCap > 0) {
    return 0;
}
```

A cleaner contract would require a non-null destination for every decode operation and provide a separate validation-only or decoded-size operation if needed.

### Acceptance criteria

- Null source with nonzero length fails safely.
- Null destination cannot cause a write.
- Validation-only decoding, if supported, has an explicit contract.
- Pointer requirements are documented for empty and nonempty input.

### Required tests

- Null source with zero length.
- Null source with nonzero length.
- Null destination with zero capacity.
- Null destination with positive capacity.
- Valid empty encoded sequence.

---

# P1: Result semantics

## 3. Distinguish valid empty output from failure

### Current behavior

The COBS sequence:

```text
01
```

is a valid encoding of an empty byte sequence.

Decoding it successfully writes zero bytes and returns:

```cpp
0
```

The same value is used for:

- invalid COBS data
- output buffer exhaustion
- invalid pointers after correction
- successful decoding of empty input

TinyLink's current receive path ignores encoded frames shorter than five bytes, so this ambiguity does not directly break its normal packet pipeline. It remains an incorrect codec-level contract.

### Required change

Return status separately from the byte count.

Suggested model:

```cpp
enum class CobsStatus {
    Success,
    InvalidArgument,
    InvalidEncoding,
    OutputTooSmall,
    SizeOverflow
};

struct CobsResult {
    CobsStatus status;
    size_t bytesWritten;
};
```

The same result model should be considered for encoding and decoding.

### Acceptance criteria

- Valid empty output returns `Success` with zero bytes written.
- Invalid encoding is distinguishable from output exhaustion.
- Invalid arguments are distinguishable from malformed source data.
- Callers do not infer success only from a nonzero byte count.

### Required tests

- Decode `[0x01]` and verify successful zero-length output.
- Decode invalid code byte zero and verify `InvalidEncoding`.
- Decode with insufficient output capacity and verify `OutputTooSmall`.
- Decode with invalid pointer arguments and verify `InvalidArgument`.

---

## 4. Preserve the empty-input encode/decode round trip

### Current behavior

Encoding empty source input produces:

```text
01
```

and returns one byte. Decoding this valid result returns zero bytes.

The round trip is valid:

```text
source length: 0
encoded length: 1
decoded length: 0
```

The current decoder API cannot distinguish that successful result from failure.

### Required change

Use the explicit status result described above and add a fixed empty-input vector.

### Acceptance criteria

- Encoding empty input succeeds with the canonical encoded form.
- Decoding the canonical form succeeds with zero decoded bytes.
- Empty-input behavior is documented and tested independently of TinyLink's packet minimum length.

---

# P1: Destination state after failure

## 5. Define encoding output after failure

### Current behavior

`cobs_encode()` writes destination bytes while processing source data. If it later detects insufficient capacity, it returns zero after partially changing `dst`.

For example, data bytes may already have been copied here:

```cpp
dst[write_idx++] = src[read_idx];
```

before a later capacity check fails.

### Required decision

Choose one contract.

#### Preferred: prevalidate capacity

Calculate the required encoded capacity before modifying `dst`. If the destination is too small, return `OutputTooSmall` with no output changes.

#### Alternative: destructive failure

Keep one-pass encoding and document that destination contents are unspecified after failure.

### Recommendation

Because TinyLink frames are small, calculate the required encoded size first. This gives atomic capacity failure without meaningful runtime or memory cost.

### Acceptance criteria

- Required capacity is known before writing starts.
- Insufficient capacity leaves the destination unchanged, or destructive failure is explicitly documented.
- Callers never transmit output produced by a failed encoding operation.

### Required tests

- Destination one byte too small.
- Failure after one or more code blocks.
- Destination initialized with sentinel bytes and checked after failure.
- Exact-capacity success.

---

## 6. Define decoding output after failure

### Current behavior

The decoder copies complete earlier blocks before validating later blocks. A malformed later block or output-capacity failure can therefore leave a partially decoded destination.

### Required decision

Choose one model:

1. validation and decoded-size pass before copying
2. decode into temporary storage and commit after success
3. retain one-pass decoding and document that output is invalid after failure

### Recommendation

For TinyLink, the one-pass model is acceptable if all callers ignore destination contents unless the result status is `Success`. This rule must be explicit and tested.

### Acceptance criteria

- Destination contents after failure have a documented status.
- TinyLink never passes failed or partial COBS output to `Packet::unpack()`.
- Tests verify that malformed input does not trigger packet dispatch.

### Required tests

- Valid first block followed by malformed second block.
- Valid first block followed by truncated `0xFF` block.
- Output capacity exhausted after partial output.
- Verify that failure status prevents further packet processing.

---

# P1: Boundary checking

## 7. Replace addition-based bounds validation

### Current behavior

The decoder validates each block using:

```cpp
if (code == 0 || (read_idx + code - 1) > srcLen) {
    return 0;
}
```

For TinyLink's current small frames this cannot realistically overflow. As a general codec operation, addition-based bounds checking is less robust because the addition can theoretically wrap for extreme `size_t` values.

### Required change

Validate against remaining input using subtraction:

```cpp
if (code == 0) {
    return failure;
}

const size_t copiedBytes = static_cast<size_t>(code - 1);

if (copiedBytes > srcLen - read_idx) {
    return failure;
}
```

At this point, `read_idx <= srcLen` is guaranteed by the loop and the code-byte read.

### Acceptance criteria

- No block-length validation relies on potentially overflowing addition.
- A code value larger than the remaining source is rejected.
- Truncated `0xFF` blocks fail safely.
- Exact-size blocks succeed.

---

# P1: Encoded capacity calculation

## 8. Add a checked encoded-size helper

### Current behavior

The comment states:

```cpp
// Worst-case encoded length: srcLen + (srcLen / 254) + 1
```

The codec does not expose this calculation. Callers therefore use independent formulas such as `plainLength + 4` and the encoder discovers insufficient capacity only after beginning to write.

The formula also needs overflow protection for arbitrary `size_t` input.

### Required change

Provide a checked helper owned by the codec:

```cpp
inline bool try_cobs_encoded_size(size_t srcLen, size_t& required)
{
    const size_t overhead = srcLen / 254;

    if (srcLen > SIZE_MAX - overhead - 1) {
        return false;
    }

    required = srcLen + overhead + 1;
    return true;
}
```

The formula and implementation must agree at exact multiples of 254.

### Acceptance criteria

- Callers obtain required encoded capacity from the codec contract.
- Size calculation detects arithmetic overflow.
- TinyLink buffer sizes no longer duplicate approximate COBS overhead knowledge.
- Tests lock down behavior at exact 254-byte block boundaries.

### Required tests

- Source lengths 0 and 1.
- Source lengths 253, 254, and 255.
- Source lengths 507, 508, and 509.
- Maximum TinyLink plain-frame length.
- Values near `SIZE_MAX` for helper overflow handling.

---

# P2: Contract and diagnostics

## 9. Document decode buffer overlap

### Current behavior

Encoding explicitly documents:

> Destination buffer must not overlap source.

Decoding does not state whether overlap is supported.

The forward-copy algorithm may appear to work for some memory arrangements, but incidental success does not define safe in-place behavior.

### Required change

Document that source and destination ranges must not overlap for both encoding and decoding, unless in-place decoding is deliberately designed and tested.

### Acceptance criteria

- Encode and decode state the same overlap rule.
- All current call sites are reviewed for compliance.
- Tests do not rely on overlapping buffers.

---

## 10. Keep stream delimiters outside the codec

### Current behavior

The decoder documentation correctly states that encoded input excludes framing delimiters. The encoder produces only encoded content. TinyLink adds leading and trailing `0x00` bytes separately.

This separation is correct and should remain explicit.

### Required contract

- `cobs_encode()` does not write framing delimiters.
- `cobs_decode()` receives data after delimiters have been removed.
- A zero byte in encoded input is invalid.
- Stream framing and resynchronization remain the caller's responsibility.

### Acceptance criteria

- Codec tests contain no implicit delimiter handling.
- TinyLink framing tests verify delimiter handling separately.
- Embedded zero code bytes are rejected by decoding.

---

## 11. Return accurate error categories

### Current behavior

Both codec methods collapse all errors into zero.

This prevents callers from distinguishing:

- invalid arguments
- malformed encoding
- output capacity exhaustion
- size arithmetic overflow

### Required change

Use explicit status categories, for example:

```cpp
enum class CobsStatus {
    Success,
    InvalidArgument,
    InvalidEncoding,
    OutputTooSmall,
    SizeOverflow
};
```

### Acceptance criteria

- TinyLink maps codec failures to accurate diagnostics.
- Output exhaustion is not reported as malformed remote input.
- Invalid encoding does not become a CRC error later in the pipeline.
- All callers handle every status explicitly.

---

# Algorithm assessment

## Encoding correctness

The encoding loop correctly:

- reserves a byte for the current code value
- closes a block when a zero source byte is encountered
- excludes source zeroes from encoded output
- begins a new block after 254 nonzero bytes
- writes the final code byte
- checks destination capacity before each direct write

The behavior at a run of exactly 254 nonzero bytes produces the required completed block and a following code position. Golden vectors should lock down the canonical encoded representation at this boundary.

## Decoding correctness

For valid pointers and realistic TinyLink lengths, the decoder correctly:

- rejects zero code bytes
- rejects code blocks longer than remaining encoded input
- copies `code - 1` data bytes
- reinserts zero between blocks when `code < 0xFF`
- avoids appending zero after the final block
- checks output capacity before each write

No obvious out-of-bounds read or write is present under the documented pointer and capacity preconditions.

---

# Positive findings

The following characteristics should be preserved:

- no dynamic allocation
- no STL dependencies
- linear encoding and decoding
- constant auxiliary memory
- explicit destination-capacity checks
- rejection of zero code bytes
- rejection of truncated code blocks
- correct separation between COBS encoding and stream delimiters
- separate TinyLink receive and transmit buffers

---

# Required test suite

## Encoding vectors

- [ ] Empty input.
- [ ] One zero byte.
- [ ] One nonzero byte.
- [ ] Multiple consecutive zero bytes.
- [ ] Nonzero bytes followed by zero.
- [ ] Zero followed by nonzero bytes.
- [ ] Embedded zero bytes.
- [ ] 253 consecutive nonzero bytes.
- [ ] 254 consecutive nonzero bytes.
- [ ] 255 consecutive nonzero bytes.
- [ ] 508 consecutive nonzero bytes.
- [ ] Maximum TinyLink plain frame.
- [ ] Exact destination capacity.
- [ ] Destination one byte too small.
- [ ] Null source with zero length.
- [ ] Null source with nonzero length.
- [ ] Null destination.

## Decoding vectors

- [ ] Encoded empty input `[0x01]`.
- [ ] Encoded single zero `[0x01, 0x01]`.
- [ ] Code byte zero.
- [ ] Code length larger than remaining source.
- [ ] Truncated `0xFF` block.
- [ ] Multiple valid blocks.
- [ ] Final block with `code < 0xFF`.
- [ ] Final block with `code == 0xFF`.
- [ ] Embedded framing zero rejected.
- [ ] Exact output capacity.
- [ ] Output capacity one byte too small.
- [ ] Null source with nonzero length.
- [ ] Null destination.
- [ ] Empty encoded input according to the selected contract.

## Round-trip properties

For supported source lengths and random payloads, verify:

```text
decode(encode(source)) == source
```

Include payloads containing:

- [ ] no zero bytes
- [ ] only zero bytes
- [ ] alternating zero and nonzero bytes
- [ ] long nonzero runs
- [ ] random bytes
- [ ] maximum TinyLink packet size

## Mutation and fuzz tests

- [ ] Mutate every byte in known encoded vectors.
- [ ] Insert zero into encoded input.
- [ ] Remove code bytes.
- [ ] Increase and decrease code values.
- [ ] Truncate each encoded vector at every byte position.
- [ ] Fuzz the decoder with arbitrary byte arrays.
- [ ] Run under AddressSanitizer on desktop targets.
- [ ] Run under UndefinedBehaviorSanitizer on desktop targets.
- [ ] Verify that failed decoding never reaches `Packet::unpack()`.

---

# Implementation checklist

## Immediate safety fixes

- [ ] Validate `src` and `dst` in `cobs_encode()`.
- [ ] Validate `src` and `dst` in `cobs_decode()`.
- [ ] Define null source behavior for zero-length input.
- [ ] Use subtraction-based decode bounds checking.

## Result semantics

- [ ] Introduce explicit codec status values.
- [ ] Return status separately from bytes written.
- [ ] Represent successful empty output unambiguously.
- [ ] Map codec failures to accurate TinyLink diagnostics.

## Capacity and output behavior

- [ ] Add a checked encoded-size helper.
- [ ] Replace duplicated caller-side capacity formulas.
- [ ] Define destination contents after encoding failure.
- [ ] Define destination contents after decoding failure.
- [ ] Ensure callers ignore output unless status is successful.

## Contract and documentation

- [ ] Document non-overlapping source and destination for encode.
- [ ] Document non-overlapping source and destination for decode.
- [ ] Keep `0x00` stream delimiters outside the codec.
- [ ] Document canonical empty-input encoding.
- [ ] Document behavior at exact 254-byte block boundaries.

## Verification

- [ ] Add canonical COBS golden vectors.
- [ ] Add empty-input tests.
- [ ] Add exact-capacity and insufficient-capacity tests.
- [ ] Add 253, 254, 255, 508, and 509 byte tests.
- [ ] Add round-trip property tests.
- [ ] Add decoder mutation tests.
- [ ] Add decoder fuzzing.
- [ ] Run sanitizer builds.

---

# Definition of done

The `CobsCodec.h` remediation is complete when:

- [ ] Invalid pointers cannot be dereferenced.
- [ ] Successful empty output is distinguishable from failure.
- [ ] Invalid encoding and output exhaustion have different statuses.
- [ ] Required encoded capacity is calculated by a checked codec-owned helper.
- [ ] Decode bounds validation cannot overflow.
- [ ] Destination state after failure is documented and respected.
- [ ] Encode and decode buffer-overlap requirements are explicit.
- [ ] Stream delimiters remain outside the codec.
- [ ] TinyLink buffer sizing derives from the codec contract.
- [ ] Golden-vector, boundary, round-trip, mutation, fuzz, and sanitizer tests pass.

---

# Verdict

`CobsCodec.h` contains a fundamentally sound COBS implementation for TinyLink's current small frames. The encoding and decoding loops are bounds-aware and appear algorithmically correct under valid-pointer preconditions.

The immediate corrections are:

1. validate pointer arguments
2. separate status from byte count
3. define partial-output behavior
4. centralize checked encoded-size calculation

Its current weaknesses are primarily API safety and failure semantics rather than a visible COBS algorithm defect.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/AckMessage.h`

The internal wire structures should be reviewed next to verify exact layout, field widths, initialization, and serialization assumptions.
