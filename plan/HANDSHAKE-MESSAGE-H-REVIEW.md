# Review of `src/internal/protocol/HandshakeMessage.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/HandshakeMessage.h`

The file is structurally safe and its one-byte layout is protected by a compile-time assertion. The important problem is semantic: the field named `version` is not used as a protocol version. In `TinyLink.h`, it is used as a request/response discriminator, while values outside the documented range are still treated as successful handshakes.

---

# Summary

## Main findings

1. `version` currently has two conflicting meanings.
2. The current receiver accepts undefined handshake values as successful handshakes.
3. Packing is unnecessary for a one-byte wire message.
4. Raw structure copying bypasses semantic validation.
5. `HandshakeMessage` is described as internal but exposed through the public callback API.
6. The handshake does not currently negotiate or verify protocol compatibility.
7. Documentation should be corrected after the wire model is clarified.

---

# P0: Correct the handshake wire semantics

## 1. Separate handshake kind from protocol version

### Current behavior

The header documents the field as a protocol version:

```cpp
uint8_t version; ///< Protocol version (0 = initial)
```

The structure-level comment also says that the sender advertises its protocol version.

However, `TinyLink.h` applies this meaning:

```text
0 = initial handshake request
1 = handshake response
```

These values represent handshake message kinds or phases, not protocol versions.

The field therefore cannot independently express both:

- whether the message is a request or response
- which wire-protocol version the sender supports

### Consequences

- A protocol version of `1` cannot be distinguished from a handshake response.
- A future protocol version of `2` is not represented meaningfully.
- Documentation and implementation disagree.
- Compatibility checks cannot be added cleanly without changing the message model.

### Required change

Rename the current concept to a handshake kind and represent it explicitly:

```cpp
enum class HandshakeKind : uint8_t {
    Request = 0,
    Response = 1
};
```

If compatibility negotiation is required, add a separate protocol-version field:

```cpp
struct HandshakeMessage {
    HandshakeKind kind;
    uint8_t protocolVersion;
};
```

The wire format should then encode each field explicitly rather than copying the structure representation.

### Acceptance criteria

- A field called `version` represents an actual protocol version.
- Request and response semantics use a separate field or type.
- Future protocol versions cannot be confused with handshake phases.
- The current protocol version is defined centrally.
- Unsupported kinds and protocol versions are rejected explicitly.
- Golden vectors specify exact request and response bytes.

### Required tests

- Request-kind encode and decode.
- Response-kind encode and decode.
- Current protocol-version encode and decode if introduced.
- Unsupported kind.
- Unsupported version.
- Same-version peer compatibility.
- Different-version peer incompatibility.

---

## 2. Reject undefined handshake values

### Current behavior

The current engine treats `0` specially by sending a reply, but every decoded byte eventually causes the connection state to advance.

The effective behavior is:

```text
0   -> request and connection established
1   -> response and connection established
2   -> connection established
255 -> connection established
```

The defect is implemented in `TinyLink.h`, but the untyped `uint8_t version` field makes it easy to accept arbitrary values.

### Required change

Decode and validate the raw kind before changing connection state:

```cpp
switch (rawKind) {
    case static_cast<uint8_t>(HandshakeKind::Request):
        break;

    case static_cast<uint8_t>(HandshakeKind::Response):
        break;

    default:
        return HandshakeDecodeStatus::InvalidKind;
}
```

If protocol version is present, validate it independently after validating the kind.

### Acceptance criteria

- Only defined handshake kinds are accepted.
- Unknown kinds never establish a connection.
- Unsupported versions never establish a connection.
- Invalid handshakes update a specific diagnostic.
- Invalid handshakes do not invoke a successful handshake or connection callback.
- Invalid handshakes do not disturb a pending application ACK state.

### Required tests

- Raw kind `0`.
- Raw kind `1`.
- Raw kind `2`.
- Raw kind `255`.
- Invalid kind while connecting.
- Invalid kind while connected.
- Invalid kind while awaiting a data ACK.
- Unsupported protocol version.

---

# P1: Replace raw structure serialization

## 3. Remove unnecessary packing

### Current behavior

The one-byte message uses the shared packing abstraction:

```cpp
TINYLINK_PACKED_BEGIN
struct HandshakeMessage {
    uint8_t version;
} TINYLINK_PACKED;
TINYLINK_PACKED_END
```

A structure containing one `uint8_t` field already has a one-byte payload representation on the supported implementation model. Packing provides no meaningful additional protection for this message.

The existing size assertion is still useful while the structure remains:

```cpp
static_assert(sizeof(HandshakeMessage) == 1,
              "HandshakeMessage must be exactly 1 byte");
```

### Required change

- Remove packing from the semantic handshake message.
- Encode the wire byte or bytes explicitly.
- Decode raw bytes explicitly and validate them before constructing the semantic message.

For the current one-byte kind model:

```cpp
output[0] = static_cast<uint8_t>(message.kind);
```

If protocol version is added:

```cpp
output[0] = static_cast<uint8_t>(message.kind);
output[1] = message.protocolVersion;
```

### Acceptance criteria

- Handshake interoperability does not depend on packing macros.
- Every wire byte is assigned explicitly.
- Exact wire length is defined independently of `sizeof` a semantic type.
- Golden vectors verify the complete payload.

---

## 4. Validate raw bytes before creating semantic values

### Current behavior

The receive path copies the payload directly into the structure:

```cpp
HandshakeMessage message;
memcpy(&message, payload, sizeof(message));
```

This is memory-safe for a valid one-byte payload, but every raw value becomes an accepted object value before semantic validation.

### Required change

Introduce explicit decoding with a typed result:

```cpp
enum class HandshakeDecodeStatus : uint8_t {
    Success,
    InvalidLength,
    InvalidKind,
    UnsupportedVersion
};
```

The decoder should:

1. verify the exact payload length
2. validate the raw handshake kind
3. validate protocol version if present
4. create the semantic event only after all checks succeed

### Acceptance criteria

- Empty payload is rejected.
- Oversized payload is rejected.
- Undefined kind is rejected.
- Unsupported version is rejected.
- Semantic output remains unchanged on decode failure.
- Connection state changes only after successful decoding.

---

# P1: Correct the public/internal API boundary

## 5. Do not expose `HandshakeMessage` through the public callback

### Current behavior

The file states:

> Internal header: not part of the public API surface. Users should not depend on the wire layout defined here.

But `TinyLink.h` publishes:

```cpp
typedef void (*HandshakeCallback)(const HandshakeMessage& message);
```

A consumer registering this callback necessarily depends on the internal type, its name, members, and semantics. It is therefore part of the public source API despite the comment.

### Required decision

Choose a public semantic API that is independent of the wire payload.

## Option A: Public handshake event

```cpp
struct HandshakeEvent {
    HandshakeKind kind;
    uint8_t protocolVersion;
};
```

## Option B: Callback arguments

```cpp
typedef void (*HandshakeCallback)(
    HandshakeKind kind,
    uint8_t protocolVersion);
```

## Option C: Connection-state notification

```cpp
typedef void (*ConnectionCallback)(ConnectionState state);
```

### Recommendation

Expose a connection-state event rather than raw handshake traffic. Most consumers need to know that the link connected, failed, disconnected, or restarted. They generally do not need the internal request or response message.

A separate diagnostic callback can expose protocol negotiation details if advanced users need them.

### Acceptance criteria

- No internal handshake wire type appears in public headers.
- Public callbacks describe application-relevant state.
- Internal wire representation can evolve without breaking callback consumers.
- Consumers require only public headers.
- Invalid raw handshakes never produce successful connection events.

### Required tests

- Consumer compiles using public headers only.
- Connection callback fires after successful negotiation.
- Connection callback does not fire for invalid kind.
- Connection callback does not fire for unsupported version.
- Changing internal handshake wire layout does not change the public callback contract.

---

# P1: Add real protocol compatibility handling

## 6. Decide and implement a versioning strategy

### Current behavior

Despite the field name, the current handshake does not verify compatible protocol versions.

The protocol therefore has no defined mechanism for detecting compatibility after changes such as:

- ACK wire-layout correction
- Fletcher-16 correction
- explicit wire serialization
- status validation
- message-type reservation changes
- handshake semantic changes

Some of these changes may alter the bytes accepted or emitted by peers.

### Required decision

Decide whether TinyLink promises wire compatibility across versions.

For a deliberately minimal protocol, the handshake can remain small:

```cpp
struct HandshakeWireMessage {
    uint8_t kind;
    uint8_t protocolVersion;
};
```

The receiver should then:

- accept versions it supports
- reject unsupported versions
- report incompatibility through a defined state or result
- avoid reporting the link as connected before compatibility is established

If ranges are supported, define minimum and maximum compatible versions explicitly rather than relying on equality by accident.

### Acceptance criteria

- Current protocol version is defined in one canonical location.
- Compatibility rules are documented.
- Peers verify compatibility before reporting connected state.
- Unsupported versions produce a deterministic outcome.
- Wire-breaking changes update protocol version intentionally.
- Tests cover compatible and incompatible peer combinations.

---

# P2: Documentation corrections

## 7. Correct comments after fixing the model

### Current behavior

The structure-level comment says:

```cpp
@param version Protocol version advertised by the sender
```

The member comment says:

```cpp
Protocol version (0 = initial)
```

The implementation uses `0` for request and `1` for response.

The comments cannot be made accurate without first deciding whether the byte represents kind or version.

### Required change

If the current one-byte format temporarily remains, name it honestly:

```cpp
uint8_t kind; ///< 0 = request, 1 = response
```

If a real version field is added, document its compatibility semantics separately.

### Acceptance criteria

- Comments describe actual runtime behavior.
- Request and response are not called protocol versions.
- Version documentation states how compatibility is determined.

---

## 8. Replace `@param` documentation for the struct field

### Current behavior

The structure uses:

```cpp
@param version
```

`@param` is intended for callable parameters rather than structure fields.

### Required change

Use prose in the structure comment and a concise member-level comment.

For example:

```cpp
/**
 * @brief Internal handshake request or response.
 *
 * The message kind identifies whether the frame starts or acknowledges
 * a handshake exchange.
 */
```

### Acceptance criteria

- Generated documentation does not list structure fields as function parameters.
- Field documentation is concise and non-duplicative.

---

## 9. Correct the `@file` path

### Current behavior

The file declares:

```cpp
@file internal/HandshakeMessage.h
```

The reviewed path is:

```text
src/internal/protocol/HandshakeMessage.h
```

### Required change

Use either:

```cpp
@file HandshakeMessage.h
```

or:

```cpp
@file internal/protocol/HandshakeMessage.h
```

Apply the selected convention consistently throughout the repository.

### Acceptance criteria

- Generated file documentation uses the correct path or filename.
- All headers use the same `@file` convention.

---

# Positive findings

The following characteristics should be retained where relevant.

## Exact size assertion

```cpp
static_assert(sizeof(HandshakeMessage) == 1,
              "HandshakeMessage must be exactly 1 byte");
```

This correctly locks down the current one-byte structure assumption while structure serialization remains.

## Fixed-width field

The current use of `uint8_t` gives the field a deterministic one-byte representation.

## Minimal message size

The message contains no pointers, dynamic storage, variable-length data, or architecture-sized fields.

## No byte-order ambiguity

The current one-byte format has no endianness concern. A future multibyte version or capability field would require explicit byte order.

---

# Required test suite

## Wire encoding

- [ ] Request encodes to its canonical byte sequence.
- [ ] Response encodes to its canonical byte sequence.
- [ ] Protocol version encodes separately if introduced.
- [ ] Expected bytes are fixed test data rather than copied structure bytes.
- [ ] Exact output capacity succeeds.
- [ ] Insufficient output capacity fails without partial semantic success.

## Decode validation

- [ ] Request value accepted.
- [ ] Response value accepted.
- [ ] Raw value `2` rejected under the current kind model.
- [ ] Raw value `255` rejected.
- [ ] Empty payload rejected.
- [ ] Payload longer than the defined handshake size rejected.
- [ ] Unsupported protocol version rejected.
- [ ] Semantic output remains unchanged after decode failure.

## State-machine behavior

- [ ] Request receives one response.
- [ ] Response completes an initiated handshake.
- [ ] Invalid kind does not establish a connection.
- [ ] Unsupported version does not establish a connection.
- [ ] Simultaneous requests connect both compatible peers.
- [ ] Unexpected response while already connected follows a documented policy.
- [ ] Request while awaiting a data ACK preserves the pending ACK state.
- [ ] Malformed handshake does not disturb valid pending state.
- [ ] Invalid handshake does not invoke a successful callback.

## Public API

- [ ] Public consumers do not include the internal handshake header.
- [ ] Public connection notifications do not expose wire-layout types.
- [ ] Internal handshake wire changes do not alter the public callback contract.
- [ ] Public event contains only validated semantic data.

## Compatibility

- [ ] Same-version peers connect.
- [ ] Supported compatible versions connect according to policy.
- [ ] Unsupported-version peers reject the connection.
- [ ] Version mismatch produces a defined diagnostic or state.
- [ ] Wire-breaking changes are tied to an intentional version change.

---

# Implementation checklist

## Handshake model

- [ ] Decide whether the current byte represents kind or version.
- [ ] Rename `version` to `kind` if it remains a request/response discriminator.
- [ ] Introduce `HandshakeKind : uint8_t`.
- [ ] Reject unknown kind values.
- [ ] Add a separate protocol-version field if compatibility negotiation is required.
- [ ] Define the current protocol version centrally.
- [ ] Define compatibility policy.

## Wire codec

- [ ] Replace raw structure serialization with explicit encode and decode functions.
- [ ] Remove unnecessary packing from the one-byte message.
- [ ] Define exact wire payload length independently of `sizeof`.
- [ ] Validate exact input length.
- [ ] Validate kind before creating semantic output.
- [ ] Validate protocol version before state transition.
- [ ] Add fixed golden vectors.

## State machine

- [ ] Prevent undefined kinds from establishing a connection.
- [ ] Prevent unsupported versions from establishing a connection.
- [ ] Preserve pending ACK state when valid handshake traffic arrives.
- [ ] Preserve pending ACK state when malformed handshake traffic arrives.
- [ ] Define handling of repeated requests and responses.
- [ ] Define connection failure or incompatibility state.

## Public API

- [ ] Remove `HandshakeMessage` from the public callback.
- [ ] Prefer a semantic connection-state callback.
- [ ] Ensure public consumers need only public headers.
- [ ] Separate diagnostics from ordinary connection notification where useful.

## Documentation

- [ ] Replace incorrect version terminology.
- [ ] Replace struct `@param` tags.
- [ ] Correct the `@file` path.
- [ ] Document handshake kinds.
- [ ] Document protocol compatibility behavior.

## Verification

- [ ] Add request and response wire vectors.
- [ ] Add invalid-kind tests.
- [ ] Add unsupported-version tests.
- [ ] Add simultaneous-handshake tests.
- [ ] Add pending-ACK interaction tests.
- [ ] Add public API boundary tests.

---

# Definition of done

The `HandshakeMessage.h` remediation is complete when:

- [ ] Handshake kind and protocol version are separate concepts.
- [ ] All raw kind values are validated.
- [ ] Unsupported versions cannot establish a connection.
- [ ] Handshake wire fields are encoded and decoded explicitly.
- [ ] Packing is not required for handshake interoperability.
- [ ] Connection state changes only after complete validation.
- [ ] Public callbacks no longer expose the internal wire message.
- [ ] Compatibility behavior is documented and tested.
- [ ] Doxygen field comments and file paths are accurate.
- [ ] Golden-vector, invalid-kind, version, state-machine, and public API tests pass.

---

# Verdict

`HandshakeMessage.h` is memory-safe and layout-stable in its current one-byte form. The important flaw is semantic: `version` is not used as a protocol version. It is used as a request/response discriminator, while undefined values are currently accepted as successful handshakes by `TinyLink.h`.

The correction should begin by separating:

```text
handshake kind
```

from:

```text
protocol version
```

Only after those concepts are explicit should the public callback and final handshake wire representation be finalized.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/LogMessage.h`
