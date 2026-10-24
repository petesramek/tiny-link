# Review of `src/internal/codec/Packet.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/codec/Packet.h`

The file has a good basic shape and performs important length and capacity checks. However, it contains two definite correctness defects and several API-contract weaknesses that should be corrected before the packet codec is considered reliable.

---

# Summary

## Confirmed defects

1. `pack()` accepts a null payload with a nonzero payload length.
2. Payload lengths above 255 bytes are silently truncated in the one-byte wire-length field.
3. `unpack()` can modify metadata outputs before returning failure.
4. A valid zero-length payload is indistinguishable from an unpacking error.

## Additional improvements

1. Protect packet-size calculations against invalid input.
2. Return specific decode outcomes instead of collapsing all failures to zero.
3. Decide whether this codec or the TinyLink engine owns the 64-byte payload limit.
4. Centralize packet sizing constants.
5. Document the non-overlapping buffer requirement.

---

# P0: Confirmed defects

## 1. Reject a null payload with a nonzero length

### Current behavior

`pack()` copies the payload only when both the length and pointer are nonzero:

```cpp
if (payloadLen && payload) {
    memcpy(&out[3], payload, payloadLen);
}
```

When `payloadLen > 0` and `payload == nullptr`, the function does not fail. It leaves the payload range in `out` unchanged or uninitialized, calculates a checksum over those bytes, and returns success.

If the output buffer is stack-allocated and not initialized, this can transmit unrelated stack contents.

### Required change

Reject the invalid argument combination before writing the packet:

```cpp
if (!out) {
    return 0;
}

if (payloadLen > 0 && !payload) {
    return 0;
}
```

After validation, copy whenever the payload length is nonzero:

```cpp
if (payloadLen > 0) {
    memcpy(&out[3], payload, payloadLen);
}
```

### Acceptance criteria

- `payload == nullptr` and `payloadLen > 0` returns failure.
- No checksum is calculated over uninitialized payload bytes.
- No bytes from an uninitialized output buffer can be transmitted as payload.
- `payload == nullptr` and `payloadLen == 0` follows the chosen zero-length-payload policy.

### Required tests

- Null payload with length zero.
- Null payload with length one.
- Null payload with the maximum supported length.
- Verify that the failure result is zero or the new typed error result.
- Verify that a failed operation does not claim to have produced a packet.

---

## 2. Reject payload lengths that cannot fit in the wire-length field

### Current behavior

The packet header stores payload length in one byte:

```cpp
out[2] = static_cast<uint8_t>(payloadLen);
```

But the input length is `size_t`, and no representable-range check is performed.

For example, a payload length of 256 is written to the header as zero while the function still copies 256 bytes, calculates the checksum across those bytes, and returns a 261-byte packet. That packet cannot be decoded consistently because `unpack()` derives the expected packet size from the truncated header value.

### Required change

At minimum, reject lengths larger than `UINT8_MAX` before calculating packet size or writing output:

```cpp
if (payloadLen > UINT8_MAX) {
    return 0;
}
```

TinyLink currently defines a protocol maximum payload of 64 bytes. Decide whether that limit belongs in this packet codec or in the TinyLink engine.

If `Packet.h` is specific to the TinyLink wire protocol, prefer enforcing the protocol-wide limit here through a shared constant.

### Acceptance criteria

- The function never truncates `payloadLen` into the wire header.
- Every successfully packed packet can be interpreted using its encoded length.
- Payload length 255 succeeds only if the packet codec intentionally supports it.
- Payload length 256 always fails.
- The 64-byte TinyLink limit has one documented owner.

### Required tests

- Payload length zero.
- Payload length one.
- Payload length 64.
- Payload length 65.
- Payload length 255.
- Payload length 256.
- A very large `size_t` input.

Expected success for lengths above 64 depends on the chosen ownership of the TinyLink protocol limit.

---

# P1: Failure semantics and output consistency

## 3. Leave every output unchanged when `unpack()` fails

### Current behavior

`unpack()` writes the decoded type and sequence before checking the payload output capacity:

```cpp
if (outType) {
    *outType = src[0];
}

if (outSeq) {
    *outSeq = src[1];
}

if (outPayload) {
    if (outPayloadCap < payloadLen) {
        return 0;
    }
}
```

If the packet and checksum are valid but the payload destination is too small, the function returns failure after modifying `outType` and `outSeq`.

This produces partial results from a failed operation.

### Required change

Complete every validation step before modifying any caller-owned output:

```cpp
if (outPayload && outPayloadCap < payloadLen) {
    return 0;
}

if (outType) {
    *outType = src[0];
}

if (outSeq) {
    *outSeq = src[1];
}

if (outPayload && payloadLen > 0) {
    memcpy(outPayload, &src[3], payloadLen);
}
```

### Acceptance criteria

- `outType` remains unchanged on every failure path.
- `outSeq` remains unchanged on every failure path.
- `outPayload` remains unchanged on every failure path.
- Outputs are committed only after the complete packet has passed validation.

### Required tests

Initialize all outputs with sentinel values and verify that they remain unchanged after:

- null source failure
- short frame failure
- length mismatch
- checksum mismatch
- insufficient output capacity
- unsupported payload length, if enforced by `unpack()`

---

## 4. Remove the zero-length success and error ambiguity

### Current behavior

The wire structure permits a five-byte packet with a zero-length payload:

```text
type + sequence + length + checksum
```

However, `unpack()` uses zero for every error and also returns the decoded payload length on success:

```cpp
return payloadLen;
```

A valid zero-length packet therefore returns the same value as a malformed packet.

The function comment attempts to avoid this by stating that successful payload length is greater than zero, but the function does not reject zero-length packets.

### Required decision

Choose one explicit model.

#### Preferred model: separate status from payload length

Return success separately and provide the payload length through an output parameter or result object:

```cpp
bool unpack(
    const uint8_t* src,
    size_t srcLen,
    uint8_t* outType,
    uint8_t* outSeq,
    uint8_t* outPayload,
    size_t outPayloadCap,
    size_t* outPayloadLen);
```

A typed result is better still because it can describe the exact failure reason.

#### Alternative model: forbid zero-length payloads

If the TinyLink protocol intentionally prohibits empty payloads, reject them explicitly and document the restriction:

```cpp
if (payloadLen == 0) {
    return 0;
}
```

### Recommendation

Allow the packet codec to represent an empty payload and return success independently from payload length. Message-specific rules should be enforced by the protocol layer.

### Acceptance criteria

- A caller can distinguish a valid empty payload from every failure.
- The selected zero-length policy is documented.
- `pack()` and `unpack()` enforce the same policy.
- Internal and application-message handlers define whether their message types permit empty payloads.

### Required tests

- Valid empty packet.
- Empty packet with invalid checksum.
- Empty packet with trailing bytes.
- Empty packet with metadata outputs only.
- Empty packet with a null payload output.

---

# P1: Size and bounds hardening

## 5. Prevent invalid packet-size arithmetic

### Current behavior

`packet_size()` calculates:

```cpp
return 3 + payloadLen + 2;
```

With a sufficiently large `payloadLen`, unsigned `size_t` arithmetic can wrap. `pack()` can then observe a small `need` value while still passing the original large length to `memcpy()`.

TinyLink's current normal usage does not approach this limit. The helper is nevertheless unsafe as a standalone API.

### Required change

Enforcing the representable wire-length limit before calling `packet_size()` is enough for the current format:

```cpp
if (payloadLen > UINT8_MAX) {
    return 0;
}
```

If `packet_size()` remains publicly callable, either document its accepted range or replace it with a checked operation:

```cpp
inline bool try_packet_size(size_t payloadLen, size_t& result)
{
    if (payloadLen > UINT8_MAX) {
        return false;
    }

    result = 3U + payloadLen + 2U;
    return true;
}
```

Do not introduce generic overflow machinery if one canonical maximum-payload check already guarantees safe arithmetic.

### Acceptance criteria

- No accepted payload length can overflow packet-size calculation.
- `pack()` validates payload length before capacity comparison or copying.
- Public size helpers clearly state or enforce their input range.

---

# P2: Error reporting and ownership of protocol limits

## 6. Return a typed unpack result

### Current behavior

Every `unpack()` failure returns zero, including:

- null source
- frame shorter than the minimum
- declared-length mismatch
- checksum mismatch
- insufficient output capacity
- any future unsupported payload condition

This prevents TinyLink from reporting accurate protocol statistics and is one reason unrelated errors are currently recorded as CRC failures.

### Required change

Introduce a typed result that separates status from payload length.

For example:

```cpp
enum class UnpackStatus {
    Success,
    NullSource,
    FrameTooShort,
    LengthMismatch,
    ChecksumMismatch,
    OutputTooSmall,
    PayloadTooLarge
};
```

The decoded payload length should be returned separately.

A result structure could keep the call site compact:

```cpp
struct UnpackResult {
    UnpackStatus status;
    size_t payloadLength;
};
```

### Acceptance criteria

- The caller can identify the precise reason for failure.
- A valid empty payload can return `Success` with length zero.
- TinyLink statistics map to actual failure causes.
- All callers handle the new result explicitly.

---

## 7. Establish one owner for the 64-byte TinyLink payload limit

### Current behavior

`TinyLink.h` enforces a 64-byte payload limit, while `Packet.h` inherently supports payload lengths up to the capacity of its one-byte length field.

This results in two different effective boundaries:

- packet format maximum: 255 bytes
- TinyLink engine maximum: 64 bytes

### Required decision

Choose one of these models.

#### TinyLink-specific packet codec

If `Packet.h` is part of the TinyLink wire protocol and is not intended for reuse, enforce the protocol-wide 64-byte limit here through a shared constant.

#### Generic packet codec

If `Packet.h` is intentionally generic, enforce only the one-byte maximum here and require the TinyLink engine to enforce its own 64-byte constraint.

### Recommendation

Because the file is under TinyLink's internal protocol implementation, define the 64-byte limit as a canonical protocol constant and use it in both `Packet.h` and `TinyLink.h`.

### Acceptance criteria

- The ownership of the 64-byte limit is documented.
- The same canonical constant drives buffer sizing and input validation.
- No duplicated literal `64` remains in packet-processing code.
- Boundary tests use the canonical constant.

---

## 8. Centralize packet layout constants

### Current behavior

Packet sizes are represented through repeated literals:

```cpp
3 + payloadLen + 2
```

The values mean:

- three header bytes
- two checksum bytes

The same structural values are also used elsewhere in TinyLink.

### Required change

Define canonical constants, for example:

```cpp
static const size_t PACKET_HEADER_SIZE = 3;
static const size_t PACKET_CHECKSUM_SIZE = 2;
static const size_t PACKET_MIN_SIZE =
    PACKET_HEADER_SIZE + PACKET_CHECKSUM_SIZE;
```

Use the project's supported C++ standard and naming conventions.

### Acceptance criteria

- Packet layout literals are defined in one place.
- `Packet.h` and `TinyLink.h` derive their calculations from shared constants.
- Comments do not need to duplicate arithmetic already expressed by named constants.

---

## 9. Document that input and output ranges must not overlap

### Current behavior

Both packing and unpacking use `memcpy()`:

```cpp
memcpy(&out[3], payload, payloadLen);
memcpy(outPayload, &src[3], payloadLen);
```

`memcpy()` requires its source and destination ranges not to overlap. The current TinyLink call sites appear to satisfy that requirement, but the helper contract does not state it.

### Required change

Document that input and output memory ranges must not overlap.

Use `memmove()` only if in-place or overlapping operation is an intentional feature. There is no clear need to add that behavior to the embedded codec.

### Acceptance criteria

- Public or internal API documentation states the non-overlap requirement.
- Existing call sites are reviewed for compliance.
- Tests do not rely on undefined overlapping-copy behavior.

---

# Positive findings

The following parts are sound and should be preserved.

## Exact decoded frame-length validation

```cpp
if (srcLen != expected) {
    return 0;
}
```

This rejects both truncated packets and packets with trailing bytes.

## Safe checksum indexing after length validation

After `srcLen == expected` is established, both checksum-byte reads are within the supplied source range.

## Payload output-capacity validation

```cpp
if (outPayloadCap < payloadLen) {
    return 0;
}
```

The payload copy itself is capacity-checked. Only the order of output mutation needs correction.

## Explicit checksum byte order

The checksum is encoded and decoded low byte first. That part of the wire format is deterministic across processor architectures.

## Validation-only unpacking

Allowing `outPayload == nullptr` supports packet validation and metadata extraction without copying the payload. This behavior is useful and should remain supported.

---

# Required test suite

## `packet_size()`

- [ ] Zero-length payload.
- [ ] One-byte payload.
- [ ] Maximum supported payload.
- [ ] One byte above the maximum.
- [ ] Very large `size_t` input.
- [ ] No accepted input produces wrapped arithmetic.

## `pack()`

- [ ] Null output pointer.
- [ ] Output capacity one byte too small.
- [ ] Exact output capacity.
- [ ] Larger output capacity.
- [ ] Null payload with zero length.
- [ ] Null payload with nonzero length.
- [ ] One-byte payload.
- [ ] Maximum TinyLink payload.
- [ ] Payload above the TinyLink maximum.
- [ ] Payload length 255 if supported by the codec contract.
- [ ] Payload length 256.
- [ ] Golden packet bytes for a known type, sequence, payload, and checksum.
- [ ] Successful packet can be unpacked by the same codec.
- [ ] Failed packing does not report bytes written.

## `unpack()`

- [ ] Null source.
- [ ] Source shorter than the five-byte packet minimum.
- [ ] Valid empty packet.
- [ ] Valid one-byte payload.
- [ ] Exact maximum payload.
- [ ] Declared length shorter than actual frame.
- [ ] Declared length longer than actual frame.
- [ ] Valid packet followed by trailing bytes.
- [ ] Invalid low checksum byte.
- [ ] Invalid high checksum byte.
- [ ] Null type output.
- [ ] Null sequence output.
- [ ] Null payload output for validation-only parsing.
- [ ] Exact payload output capacity.
- [ ] Payload output capacity one byte too small.
- [ ] Metadata outputs remain unchanged after failure.
- [ ] Payload output remains unchanged after failure.
- [ ] Golden packet checksum byte order.

## Property and mutation tests

- [ ] Every successfully packed supported payload can be unpacked.
- [ ] Unpacked type equals packed type.
- [ ] Unpacked sequence equals packed sequence.
- [ ] Unpacked payload equals packed payload.
- [ ] Mutating either checksum byte causes failure.
- [ ] Mutating any protected header or payload byte without updating the checksum causes failure.
- [ ] Inserting or deleting any byte causes a length or checksum failure.

---

# Implementation checklist

## Immediate correctness fixes

- [ ] Reject `payload == nullptr` when `payloadLen > 0`.
- [ ] Reject payload lengths that cannot fit in the wire-length field.
- [ ] Ensure every validation completes before modifying unpack outputs.
- [ ] Remove the zero-length success and error ambiguity.

## Protocol consistency

- [ ] Decide whether `Packet.h` enforces the TinyLink 64-byte maximum.
- [ ] Centralize packet header, checksum, and payload-size constants.
- [ ] Use the shared constants in `TinyLink.h`.

## Diagnostics and API behavior

- [ ] Introduce a typed unpack result.
- [ ] Map packet failures to accurate TinyLink statistics.
- [ ] Document validation-only unpacking.
- [ ] Document non-overlapping buffer requirements.

## Verification

- [ ] Add all boundary tests.
- [ ] Add golden packet vectors.
- [ ] Add output-unchanged-on-failure tests.
- [ ] Add pack/unpack round-trip property tests.
- [ ] Add byte-mutation tests.

---

# Definition of done

The `Packet.h` remediation is complete when:

- [ ] Null nonempty payloads are rejected.
- [ ] No accepted payload length can be truncated into the wire header.
- [ ] No accepted payload length can overflow packet-size arithmetic.
- [ ] A valid zero-length packet has an unambiguous success result, or zero-length payloads are explicitly prohibited.
- [ ] Every failed unpack leaves all outputs unchanged.
- [ ] Callers receive a specific failure category.
- [ ] The TinyLink payload maximum has one canonical owner.
- [ ] Packet layout constants are shared rather than duplicated.
- [ ] Golden-vector, boundary, mutation, and round-trip tests pass.
- [ ] All `Packet.h` callers are updated and reviewed for the revised contract.

---

# Verdict

`Packet.h` is compact and already handles exact frame-length matching, checksum validation, and payload output capacity correctly. There is no obvious decode-buffer overrun in the reviewed code.

The null nonempty payload path is nevertheless a real flaw because it can checksum and transmit uninitialized output bytes. The unchecked conversion from `size_t` to a one-byte payload length is another definite codec-boundary defect.

These issues should be fixed before broader packet or state-machine refactoring.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/codec/CobsCodec.h`

That file is the next priority because every received frame passes through COBS decoding before packet validation.
