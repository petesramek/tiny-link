# Review of `src/internal/protocol/AckMessage.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/AckMessage.h`

The file is small and mostly sound. It defines a compact ACK/NACK payload and protects its expected size with a compile-time assertion. The main concerns are the underlying representation of `TinyStatus`, validation of raw status values, duplicate sequence representation, direct packed-struct serialization, and leakage of an internal type through the public callback API.

---

# Summary

## Main findings

1. `TinyAck` depends on `TinyStatus` having an explicit one-byte wire representation.
2. Raw wire bytes can become `TinyStatus` values without validation.
3. The acknowledged sequence is duplicated in the packet header and ACK payload.
4. A two-byte wire message does not need packed-struct serialization.
5. `TinyAck` is documented as internal but is exposed through TinyLink's public ACK callback.
6. Doxygen field documentation and the `@file` path should be corrected.
7. `Packed.h` must be reviewed before the packing behavior is considered fully verified.

---

# P0: Define and validate the status representation

## 1. Guarantee that `TinyStatus` is exactly one byte

### Current behavior

The ACK payload is defined as:

```cpp
struct TinyAck {
    uint8_t   seq;
    TinyStatus result;
};
```

The following assertion guarantees that the complete structure is two bytes on the current compiler:

```cpp
static_assert(sizeof(TinyAck) == 2,
              "TinyAck must be exactly 2 bytes");
```

This is useful, but the wire representation of `TinyStatus` is still defined indirectly. Packing controls alignment and padding. It does not itself define an enum's underlying type.

### Required change

In `src/protocol/Status.h`, define `TinyStatus` with an explicit one-byte underlying type:

```cpp
enum class TinyStatus : uint8_t {
    // Status values
};
```

Add a direct assertion near the enum or wire codec:

```cpp
static_assert(sizeof(TinyStatus) == 1,
              "TinyStatus must have an 8-bit wire representation");
```

Every enumerator must fit in the declared one-byte representation.

### Acceptance criteria

- `TinyStatus` explicitly uses `uint8_t` as its underlying type.
- `sizeof(TinyStatus) == 1` is directly asserted.
- Every declared status value fits in one byte.
- The ACK wire format does not depend on the compiler's default enum type.
- The same assertions pass on every supported compiler and target.

### Required tests

- Compile-time assertion for `sizeof(TinyStatus) == 1`.
- Compile-time assertion for `sizeof(TinyAck) == 2` while the struct layout remains.
- Build verification on AVR, ARM, ESP32, GCC, Clang, and MSVC targets used by the project.

---

## 2. Validate raw ACK status values during decoding

### Current behavior

`TinyLink.h` decodes the ACK payload by copying two wire bytes into `TinyAck`:

```cpp
TinyAck ack;
memcpy(&ack, ackBuf, sizeof(ack));
```

The second byte therefore becomes the object representation of `TinyStatus` without validation.

If not all 256 possible values are defined statuses, malformed frames and future status values can enter normal ACK handling as undeclared enum values.

### Required decision

Choose and document one policy.

#### Reject unknown status values

Use explicit decoding and reject any raw result byte that is not a declared `TinyStatus` value:

```cpp
const uint8_t rawResult = payload[1];

if (!is_valid_tiny_status(rawResult)) {
    return AckDecodeStatus::InvalidResult;
}

ack.result = static_cast<TinyStatus>(rawResult);
```

#### Preserve unknown values for forward compatibility

If unknown future values need to survive decoding, do not represent the raw field solely as `TinyStatus`. Preserve the raw byte explicitly and expose a safe interpretation layer.

### Recommendation

Reject unknown values in the current protocol. TinyLink does not yet have a defined version or capability-negotiation mechanism that would make unknown result values safely actionable.

### Acceptance criteria

- The ACK decoder defines whether unknown status values are rejected or preserved.
- Invalid values cannot silently enter ordinary ACK handling.
- Callback consumers cannot assume that an arbitrary wire byte is a declared status.
- Switching over status values has a defined fallback path.

### Required tests

- Every declared `TinyStatus` value.
- Raw value zero.
- Highest valid status value.
- Values immediately above the highest valid value.
- Representative undefined values such as `0x7F` and `0xFF` where invalid.
- An invalid status does not clear `AWAITING_ACK`.
- An invalid status does not invoke a successful ACK callback.

---

# P1: Eliminate or validate duplicate sequence data

## 3. Choose one authoritative acknowledged sequence

### Current behavior

The packet envelope already contains a sequence byte:

```text
type + sequence + payload length
```

The ACK payload repeats the sequence:

```cpp
struct TinyAck {
    uint8_t seq;
    TinyStatus result;
};
```

The current sender places the same value in both locations. The receiver unpacks both the packet-header sequence and `TinyAck::seq`, but does not validate their relationship or compare either one with the pending transmission.

### Required decision

Choose one of these representations.

## Option A: Packet-header sequence identifies the acknowledged frame

The ACK payload contains only the result:

```cpp
struct TinyAck {
    TinyStatus result;
};
```

This reduces the internal payload to one byte and removes duplicated state.

## Option B: ACK payload sequence identifies the acknowledged frame

The packet-header sequence identifies the ACK frame itself, while `TinyAck::seq` identifies the original data frame.

This model is useful only if ACK frames have their own independent sequence identity.

### Recommendation

For TinyLink's current stop-and-wait model, use the packet-header sequence as the acknowledged data-frame sequence and remove `seq` from the ACK payload.

If both fields remain, enforce equality:

```cpp
if (packetSequence != ack.seq) {
    return AckDecodeStatus::SequenceMismatch;
}
```

The receiver must then compare the authoritative acknowledged sequence with the pending transmitted sequence before leaving `AWAITING_ACK`.

### Acceptance criteria

- The protocol specifies exactly which field identifies the acknowledged frame.
- The same information is not transmitted twice without validation.
- A header/payload sequence mismatch is rejected.
- A stale, duplicate, or unrelated ACK cannot clear pending state.
- Golden vectors reflect the selected representation.

### Required tests

- Matching sequence.
- Header/payload sequence mismatch if both remain.
- ACK for the pending sequence.
- ACK for an older sequence.
- ACK for a future sequence.
- Duplicate ACK after pending state has cleared.
- Sequence wraparound from `255` to `0`.

---

# P1: Replace packed object serialization

## 4. Encode and decode ACK fields explicitly

### Current behavior

Sending treats the in-memory representation of `TinyAck` as wire bytes:

```cpp
reinterpret_cast<const uint8_t*>(&ack)
```

Receiving copies wire bytes directly into the object:

```cpp
memcpy(&ack, ackBuf, sizeof(ack));
```

The packing macros and size assertion reduce layout risk, but this approach still makes compiler object representation part of the wire protocol.

For a one- or two-byte ACK payload, explicit serialization is simpler and safer.

### Required change

Introduce explicit ACK wire encoding and decoding.

If both sequence and status remain:

```cpp
static const size_t TINY_ACK_WIRE_SIZE = 2;

output[0] = ack.seq;
output[1] = static_cast<uint8_t>(ack.result);
```

If sequence moves exclusively to the packet header:

```cpp
static const size_t TINY_ACK_WIRE_SIZE = 1;

output[0] = static_cast<uint8_t>(ack.result);
```

Decoding must validate length, sequence policy, and raw status before constructing the semantic ACK result.

### Suggested responsibilities

```cpp
enum class AckDecodeStatus : uint8_t {
    Success,
    InvalidLength,
    InvalidResult,
    SequenceMismatch
};
```

The exact public and internal result types should follow the project's API conventions.

### Acceptance criteria

- Each ACK wire byte is assigned explicitly.
- Each decoded field is validated explicitly.
- Compiler packing is no longer required for ACK interoperability.
- Raw unknown status values follow the selected policy.
- Golden vectors verify every ACK byte.

### Required tests

- Exact encode vector for each valid status.
- Exact decode vector for each valid status.
- Truncated ACK payload.
- ACK payload with trailing bytes.
- Invalid status byte.
- Header/payload sequence mismatch if both remain.
- Encode followed by decode preserves semantic values.

---

# P1: Correct the public/internal API boundary

## 5. Do not expose an internal wire type through the public callback

### Current behavior

The header states:

> Internal header: not part of the public API surface. Users should not depend on the wire layout defined here.

However, `TinyLink.h` publishes this callback type:

```cpp
typedef void (*AckCallback)(const TinyAck& ack);
```

and this registration method:

```cpp
void onAckReceived(AckCallback cb);
```

Any user registering the callback depends on the `TinyAck` type, its namespace, members, and semantic contract. The type is therefore part of the source-level public API even if its packed wire layout is intended to remain internal.

### Required decision

Choose one API model.

## Option A: Make the semantic type public

Move a stable semantic ACK result to a public protocol header:

```cpp
struct AckResult {
    uint8_t sequence;
    TinyStatus result;
};
```

The public type does not need to be packed and must not be serialized through object representation.

## Option B: Use callback parameters

Expose stable values directly:

```cpp
typedef void (*AckCallback)(uint8_t sequence,
                            TinyStatus result);
```

### Recommendation

Introduce a public semantic `AckResult` type if the callback is expected to evolve or carry additional metadata. Keep the ACK wire codec and packed representation internal.

### Acceptance criteria

- No internal-only wire type appears in the supported public API.
- Public ACK callback semantics are documented.
- Internal wire format can change without requiring users to include internal headers.
- Public types are not marked packed unless packing is independently needed for application memory layout.

### Required tests

- A consumer can register an ACK callback using public headers only.
- Internal ACK wire representation can be changed without changing the public callback signature.
- Callback receives the validated acknowledged sequence and result.

---

# P2: Documentation corrections

## 6. Replace `@param` documentation for struct fields

### Current behavior

The structure comment uses:

```cpp
@param seq
@param result
```

`@param` is intended for callable parameters rather than structure members.

The members already contain field-level comments:

```cpp
uint8_t seq;       ///< ...
TinyStatus result; ///< ...
```

### Required change

Use prose in the structure-level comment and retain concise member comments:

```cpp
/**
 * @brief ACK/NACK result for a data frame.
 *
 * `seq` identifies the acknowledged data frame.
 * `result` reports whether processing succeeded.
 */
```

If sequence is removed from the payload, update the documentation so that it does not imply the semantic result must mirror the internal wire layout.

### Acceptance criteria

- Generated documentation does not list struct fields as function parameters.
- Each field is documented once at the most useful location.
- Documentation matches the selected sequence representation.

---

## 7. Correct the `@file` path

### Current behavior

The file comment declares:

```cpp
@file internal/AckMessage.h
```

The reviewed repository path is:

```text
src/internal/protocol/AckMessage.h
```

### Required change

Use either the local filename:

```cpp
@file AckMessage.h
```

or the accurate source-relative path:

```cpp
@file internal/protocol/AckMessage.h
```

Use the same convention consistently across the repository.

### Acceptance criteria

- The generated documentation identifies the file accurately.
- All source headers follow one `@file` path convention.

---

# Dependency review required

## `src/protocol/Status.h`

This file must be reviewed next to confirm:

- explicit enum underlying type
- exact status numeric values
- whether zero is a valid status
- whether values are contiguous
- whether all 256 byte values are meaningful
- whether status values are stable wire identifiers or only source-level values
- whether status names conflate protocol errors, transport errors, and local implementation errors

## `src/internal/protocol/Packed.h`

Review the packing abstraction to confirm:

- GCC and Clang attribute expansion
- AVR behavior
- MSVC `#pragma pack` push/pop symmetry
- restoration of previous packing state
- safety across nested includes
- repeated use in one translation unit
- no packing-state leakage into unrelated types

The existing `sizeof(TinyAck) == 2` assertion provides useful local protection, but it does not replace review of the shared packing macros used by other protocol structures.

---

# Positive findings

The following characteristics should be retained where still relevant.

## Exact size assertion

```cpp
static_assert(sizeof(TinyAck) == 2,
              "TinyAck must be exactly 2 bytes");
```

This is a valuable compile-time guard while packed struct serialization remains.

## Fixed-width sequence field

```cpp
uint8_t seq;
```

The sequence width is explicit and portable.

## Small bounded message

The ACK payload contains no pointers, dynamic storage, variable-length fields, or embedded platform-sized integer types.

## Internal organization intent

Placing wire-specific details under `internal/protocol` is directionally correct. The public callback API must be changed so that this boundary is real rather than documentary only.

---

# Required test suite

## Compile-time representation tests

- [ ] `sizeof(TinyStatus) == 1`.
- [ ] `sizeof(TinyAck) == 2` while both fields remain in the packed structure.
- [ ] `offsetof(TinyAck, seq) == 0` while struct serialization remains.
- [ ] `offsetof(TinyAck, result) == 1` while struct serialization remains.
- [ ] Assertions pass on all supported compilers.

## Encoding vectors

If both fields remain, verify exact bytes:

```text
byte 0: acknowledged sequence
byte 1: status code
```

If sequence moves to the packet header, verify the one-byte ACK payload:

```text
byte 0: status code
```

Expected bytes must be fixed test data. They must not be generated by copying a `TinyAck` object.

## Decode validation

- [ ] Every valid status.
- [ ] Raw status zero.
- [ ] Highest valid status.
- [ ] Representative undefined status values.
- [ ] Truncated ACK payload.
- [ ] ACK payload with trailing bytes.
- [ ] Header/payload sequence mismatch if both remain.
- [ ] Valid status with stale sequence.
- [ ] Valid status with future sequence.
- [ ] Duplicate ACK.

## Public API tests

- [ ] ACK callback is usable through public headers only.
- [ ] Public API does not require `internal/protocol/AckMessage.h`.
- [ ] Internal wire-layout changes do not alter the public ACK callback contract.
- [ ] Callback receives only validated semantic values.

## State-machine integration

- [ ] Matching ACK clears pending transmission.
- [ ] Mismatching ACK does not clear pending transmission.
- [ ] Invalid status does not clear pending transmission.
- [ ] Duplicate ACK is ignored or reported as unexpected.
- [ ] Stale ACK is ignored or reported as unexpected.
- [ ] Sequence wraparound works.

---

# Implementation checklist

## Status representation

- [ ] Review `src/protocol/Status.h`.
- [ ] Define `TinyStatus : uint8_t`.
- [ ] Add `sizeof(TinyStatus) == 1` assertion.
- [ ] Verify every numeric status value.
- [ ] Define handling of unknown raw values.

## Sequence model

- [ ] Decide whether the packet header or ACK payload owns the acknowledged sequence.
- [ ] Remove duplicate sequence data where possible.
- [ ] Validate header and payload sequence equality if both remain.
- [ ] Compare the authoritative sequence with the pending transmitted sequence.

## Wire encoding

- [ ] Introduce explicit ACK encode and decode helpers.
- [ ] Stop serializing `TinyAck` through object representation.
- [ ] Validate exact ACK payload length.
- [ ] Validate raw result values before casting.
- [ ] Add fixed wire vectors.

## Public API

- [ ] Remove internal `TinyAck` from the public callback, or make a separate semantic type public.
- [ ] Ensure consumers need only public headers.
- [ ] Document ACK callback semantics independently of wire layout.

## Documentation

- [ ] Replace struct `@param` tags.
- [ ] Correct the `@file` path.
- [ ] Update comments after selecting the sequence representation.
- [ ] Review `src/internal/protocol/Packed.h`.

## Verification

- [ ] Add compile-time layout checks while struct serialization remains.
- [ ] Add valid and invalid status tests.
- [ ] Add stale, duplicate, future, and mismatched sequence tests.
- [ ] Add public API boundary tests.
- [ ] Add state-machine ACK integration tests.

---

# Definition of done

The `AckMessage.h` remediation is complete when:

- [ ] `TinyStatus` has an explicit and asserted one-byte representation.
- [ ] Unknown raw status values follow a documented policy.
- [ ] One field is authoritative for the acknowledged sequence.
- [ ] Duplicate sequence data is removed or validated.
- [ ] Only the matching pending sequence can complete an ACK wait.
- [ ] ACK fields are encoded and decoded explicitly.
- [ ] Compiler packing is no longer required for ACK wire compatibility.
- [ ] Public callbacks no longer expose an internal-only wire type.
- [ ] Doxygen comments and file paths are accurate.
- [ ] Golden-vector, invalid-status, sequence, API-boundary, and state-machine tests pass.

---

# Verdict

`AckMessage.h` is compact and protected by a useful size assertion. The structure itself is not the primary cause of TinyLink's ACK correctness flaw. The critical state-machine problem remains in `TinyLink.h`, where received ACKs are not correlated with the pending transmitted sequence.

The important corrections associated with this file are:

1. define and validate the exact one-byte representation of `TinyStatus`
2. eliminate or validate the duplicated acknowledged sequence
3. replace packed object serialization with explicit field encoding
4. separate the public semantic ACK result from the internal wire representation

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`

That file determines whether the current ACK status representation is stable and whether arbitrary wire bytes can safely become `TinyStatus` values.
