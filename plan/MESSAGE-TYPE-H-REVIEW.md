# Review of `src/protocol/MessageType.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/MessageType.h`

The file is mechanically sound, but it exposes a significant wider protocol-model inconsistency. `MessageType` defines protocol-level frame categories, while `TinyLink::sendData()` accepts an arbitrary byte as the packet type. The receive path then handles only some enum values as internal messages and treats all remaining values as application data.

The central issue is therefore not enum syntax. TinyLink must define whether the packet-header type byte represents a protocol frame kind or an application-defined message identifier.

---

# Summary

## Positive findings

1. `MessageType` has an explicit `uint8_t` representation.
2. Every declared type has an explicit ASCII wire value.
3. `message_type_from_wire()` validates raw bytes before assigning the enum.
4. Failed conversion leaves the output argument unchanged.
5. Conversion and diagnostic helpers are small, deterministic, and allocation-free.

## Main problems

1. `MessageType::Data` conflicts with the arbitrary `type` accepted by `sendData()`.
2. Reserved protocol bytes are not rejected by `sendData()`.
3. `Cmd` and `Done` have no dedicated behavior in `TinyLink.h`.
4. Unknown wire values currently fall through to application-data dispatch.
5. The receive path bypasses `message_type_from_wire()`.
6. Replacing legacy `'R'` with `'C'` is wire-breaking but not tied to protocol-version negotiation.
7. `message_type_to_wire()` can encode invalid enum objects if callers create them through casts.
8. The file needs explicit protocol-contract assertions and file-level documentation.

---

# P0: Define the meaning of the packet type byte

## 1. Protocol frame kind and application message type are conflated

### Current enum model

The enum defines protocol-level categories:

```cpp
enum class MessageType : uint8_t {
    Data      = 'D',
    Log       = 'L',
    Cmd       = 'C',
    Ack       = 'A',
    Done      = 'K',
    Handshake = 'H'
};
```

This implies that the packet-header type byte identifies one of six protocol frame categories.

### Current sending model

`TinyLink::sendData()` accepts an arbitrary byte:

```cpp
void sendData(uint8_t type, const T& payload)
```

It passes that byte directly into packet encoding. This implies that the same packet-header byte is an application-defined message identifier.

These two interpretations are incompatible.

### Concrete consequences

Application code can currently attempt:

```cpp
sendData('H', payload);
sendData('A', payload);
sendData('L', payload);
```

The peer can route these frames into internal handshake, ACK, or log handling.

At the same time, these values all currently reach ordinary data dispatch unless explicitly intercepted by `TinyLink.h`:

```cpp
sendData('D', payload);
sendData('C', payload);
sendData('K', payload);
sendData('X', payload);
```

### Required decision

Choose one protocol model.

## Model A: The header byte is a protocol frame kind

Application data is always carried under:

```cpp
MessageType::Data
```

Application-specific message identity must then live inside the data payload or in a distinct application-type field.

Conceptually:

```text
Packet header type: 'D'
Payload byte 0: application message identifier
Payload bytes 1..n: application data
```

`Cmd` and `Done` remain protocol frame kinds only if they receive distinct defined behavior.

## Model B: The header byte is an application message identifier

Most byte values belong to the application. TinyLink reserves only a documented subset for internal control frames, such as:

```text
'A' ACK
'H' Handshake
'L' Log
```

Under this model, `Data` should not be a protocol enum member because arbitrary nonreserved values already represent data messages. `Cmd` and `Done` must either become expressly reserved internal types or leave the protocol enum.

### Recommendation

The current public API behaves more like Model B, while the enum and comments imply Model A. Choose one before changing dispatch logic.

Model B is the smaller change if preserving arbitrary application types is important. Model A is cleaner if TinyLink intends to become a fully typed protocol with distinct data, command, completion, ACK, log, and handshake semantics.

### Acceptance criteria

- The packet type byte has one documented meaning.
- Protocol frame kinds and application identifiers are separate concepts.
- Application code cannot accidentally generate internal control frames.
- Receiver dispatch follows the selected model consistently.
- Packet and protocol documentation use the same terminology.
- Golden vectors cover every reserved protocol value.

---

# P0: Prevent internal-type injection through `sendData()`

## 2. Reject reserved protocol bytes in the application send path

### Current behavior

There is no validation before the caller-supplied byte is packed into a frame:

```cpp
void sendData(uint8_t type, const T& payload)
```

The public data API can therefore emit values reserved for internal traffic.

### Required change

Under the application-message-type model, centralize the internal reservation policy:

```cpp
inline bool is_reserved_message_type(uint8_t value)
{
    switch (value) {
        case static_cast<uint8_t>(MessageType::Log):
        case static_cast<uint8_t>(MessageType::Ack):
        case static_cast<uint8_t>(MessageType::Handshake):
            return true;

        default:
            return false;
    }
}
```

If all declared enum values are protocol-reserved, include every retained value in that check.

`sendData()` must reject reserved types and return a visible send result rather than silently dropping the request.

### Acceptance criteria

- Application sends cannot emit ACK, handshake, log, or other reserved internal frames.
- Rejection is visible to the caller.
- Sender and receiver use the same canonical reservation policy.
- Reserved-byte definitions are not duplicated across dispatch and send code.
- Tests cover every reserved value.

---

# P0: Correct receive classification

## 3. Unknown and unexpected types must not accidentally become application data

### Current behavior

The receive path reads the raw type byte and explicitly recognizes only:

- handshake
- ACK in `AWAITING_ACK`
- log

Everything else falls through to application-data dispatch.

This includes:

- `MessageType::Data`
- `MessageType::Cmd`
- `MessageType::Done`
- undocumented bytes
- future unsupported protocol types
- unexpected ACK frames when not awaiting an ACK

The existing helper:

```cpp
message_type_from_wire(...)
```

is not used in the reviewed receive path.

### Required change

Under the protocol-frame-kind model, validate and dispatch every known type explicitly:

```cpp
MessageType messageType;

if (!message_type_from_wire(wireType, messageType)) {
    return reject_unknown_type();
}

switch (messageType) {
    case MessageType::Data:
        return dispatchDataFrame(...);

    case MessageType::Log:
        return dispatchLogFrame(...);

    case MessageType::Cmd:
        return dispatchCommandFrame(...);

    case MessageType::Ack:
        return dispatchAckFrame(...);

    case MessageType::Done:
        return dispatchDoneFrame(...);

    case MessageType::Handshake:
        return dispatchHandshakeFrame(...);
}
```

Under the application-message-type model, classify only the reserved internal subset. Every reserved value must always route through its internal handler, even when the current state makes the frame unexpected.

State validation belongs inside or after type classification. An unexpected ACK must remain an ACK and be rejected or reported. It must never become application data.

### Acceptance criteria

- Frame classification occurs before state validation.
- Internal types never fall through into application dispatch.
- Unknown protocol kinds follow a documented policy.
- Unexpected internal frames receive a specific diagnostic.
- Future unsupported protocol values cannot silently become data.

---

# P0: Implement or remove `Cmd` and `Done`

## 4. Declared protocol values currently lack distinct behavior

### Current behavior

The enum declares:

```cpp
Cmd  = 'C'
Done = 'K'
```

The reviewed engine has no dedicated handlers for either type. Both currently behave like ordinary application data.

This makes the public protocol enum misleading. Consumers can reasonably expect each enum value to represent a supported protocol behavior.

### Required decision

For each type, choose one outcome.

## Implement it

Define:

- exact payload format
- valid states
- state transitions
- callback or polling behavior
- ACK policy
- retry policy
- error handling

## Remove it

Delete the value until its semantics are implemented.

## Move it to application conventions

If `Cmd` and `Done` are only conventional application message identifiers, move them out of the protocol-control enum.

### Recommendation

Remove `Cmd` and `Done` from the protocol enum unless their distinct protocol behavior already exists elsewhere in the repository.

Do not reserve protocol names and wire values for messages that are indistinguishable from ordinary data.

### Acceptance criteria

- Every retained `MessageType` has defined receiver behavior.
- Every retained type has documented payload and state rules.
- No enum value exists only as an undocumented alias for application data.
- Tests cover every retained type.

---

# P1: Handle the legacy command identifier deliberately

## 5. Replacing `'R'` with `'C'` is a wire compatibility change

### Current behavior

The conversion helper explicitly rejects the legacy byte:

```cpp
// Legacy 'R' is no longer accepted;
// peers must use 'C' for commands.
```

This is locally clear, but the current handshake does not negotiate a real protocol version. An old peer using `'R'` and a current peer using `'C'` can therefore complete the handshake while disagreeing on command-frame identity.

### Required change

Use one deliberate compatibility strategy:

1. increment the protocol version and reject incompatible peers
2. temporarily accept both values during a migration period
3. provide an explicit compatibility mode
4. avoid the change before any stable protocol release

### Acceptance criteria

- Incompatible peers cannot report successful compatibility.
- Wire-breaking identifier changes are associated with protocol-version handling.
- Legacy acceptance or rejection is documented.
- The legacy removal point is explicit.
- Compatibility tests cover old and new peers.

---

# P1: Lock down protocol wire identifiers

## 6. Add enum size and value assertions

### Problem

The explicit assignments are good, but the values are protocol identifiers. Accidental edits should cause an immediate failure.

### Required change

Add assertions in the header or protocol contract tests:

```cpp
static_assert(sizeof(MessageType) == 1,
              "MessageType must have an 8-bit representation");

static_assert(
    static_cast<uint8_t>(MessageType::Data) == 'D',
    "MessageType::Data wire value changed");

static_assert(
    static_cast<uint8_t>(MessageType::Log) == 'L',
    "MessageType::Log wire value changed");

static_assert(
    static_cast<uint8_t>(MessageType::Cmd) == 'C',
    "MessageType::Cmd wire value changed");

static_assert(
    static_cast<uint8_t>(MessageType::Ack) == 'A',
    "MessageType::Ack wire value changed");

static_assert(
    static_cast<uint8_t>(MessageType::Done) == 'K',
    "MessageType::Done wire value changed");

static_assert(
    static_cast<uint8_t>(MessageType::Handshake) == 'H',
    "MessageType::Handshake wire value changed");
```

Remove assertions for any values removed from the protocol enum.

### Acceptance criteria

- Existing wire identifiers cannot change silently.
- Every new protocol type has an explicit assigned byte.
- Duplicate wire values are detected by tests.
- Protocol documentation lists every reserved byte.
- `sizeof(MessageType) == 1` is verified.

---

# P1: Constrain typed-to-wire conversion

## 7. `message_type_to_wire()` does not validate enum objects

### Current behavior

The helper is:

```cpp
inline uint8_t message_type_to_wire(MessageType type) {
    return static_cast<uint8_t>(type);
}
```

This is correct when the caller passes a declared enum member. C++ still permits callers to construct an enum object from an arbitrary byte:

```cpp
auto type = static_cast<MessageType>(0xFF);
```

Passing that value to `message_type_to_wire()` returns `0xFF`.

### Required action

Treat `message_type_to_wire()` as a typed conversion helper, not a validator. Document that it requires a declared `MessageType` value.

Ensure raw input becomes `MessageType` only through `message_type_from_wire()`.

If public callers can supply `MessageType`, optionally provide:

```cpp
inline bool is_valid_message_type(MessageType type)
{
    MessageType ignored;

    return message_type_from_wire(
        static_cast<uint8_t>(type),
        ignored);
}
```

### Acceptance criteria

- Raw bytes are validated before becoming `MessageType`.
- Encoding paths receive only known typed values.
- Invalid cast enum objects cannot enter normal protocol encoding.
- The helper's precondition is documented.

---

# P2: Preserve useful helper behavior

## 8. Keep `message_type_to_string()` diagnostic-only

### Current behavior

The diagnostic helper returns:

```cpp
"Unknown"
```

for an invalid enum object.

This is safe and useful for diagnostics. It prevents invalid values from causing undefined lookup behavior.

### Required action

Retain the fallback, but do not use it as protocol validation. Unknown values still require rejection or explicit unexpected-type handling in dispatch.

### Acceptance criteria

- Invalid values produce `"Unknown"` for diagnostics.
- Normal dispatch never treats the diagnostic fallback as successful validation.
- Unknown-type diagnostics can preserve the original raw byte where useful.

---

## 9. Preserve output on failed raw conversion

### Current behavior

`message_type_from_wire()` assigns `out` only in successful switch branches. The default branch returns `false` without modification.

This is good API behavior.

### Required action

Document and test that failed conversion leaves `out` unchanged.

### Acceptance criteria

- Every valid byte assigns one enum value.
- Every invalid byte returns `false`.
- Failed conversion leaves the output unchanged.
- No duplicate switch cases are possible.

---

# P2: Public naming and documentation

## 10. Resolve semantics before renaming `Cmd`

`Cmd` is understandable, though `Command` would be more descriptive.

Renaming it would be a public source-level change while retaining the `'C'` wire byte. The more important question is whether it belongs in this protocol enum at all.

### Recommendation

Do not perform a naming-only change. First implement, remove, or reclassify the message type.

---

## 11. Add file-level Doxygen documentation

The file lacks the Doxygen header used by the other protocol files.

Suggested header:

```cpp
/**
 * @file protocol/MessageType.h
 * @brief Wire message-type identifiers used by TinyLink frames.
 */
```

The documentation must also state whether the enum represents:

- all valid packet-header values, or
- only reserved internal values

That distinction cannot remain implicit.

### Acceptance criteria

- The file has source-root-relative `@file` documentation.
- The enum's protocol role is stated explicitly.
- Reserved and application-defined value spaces are documented.

---

# Positive findings

The following elements should be retained where applicable.

## Explicit underlying type

```cpp
enum class MessageType : uint8_t
```

provides a stable one-byte representation.

## Explicit ASCII identifiers

ASCII identifiers improve readability in captured frames and diagnostics.

## Validating raw conversion helper

`message_type_from_wire()` avoids direct arbitrary casts from wire bytes.

## Allocation-free helpers

All functions are suitable for constrained embedded targets.

## Safe diagnostic fallback

`message_type_to_string()` returns a stable string for invalid values.

---

# Required test suite

## Representation tests

- [ ] `sizeof(MessageType) == 1`.
- [ ] Every retained type has the expected ASCII value.
- [ ] No retained types share the same byte.
- [ ] Every wire identifier is explicitly assigned.

## `message_type_from_wire()`

- [ ] `'D'` maps to `Data` if retained.
- [ ] `'L'` maps to `Log`.
- [ ] `'C'` maps to `Cmd` if retained.
- [ ] `'A'` maps to `Ack`.
- [ ] `'K'` maps to `Done` if retained.
- [ ] `'H'` maps to `Handshake`.
- [ ] Legacy `'R'` follows the selected compatibility policy.
- [ ] `0x00` is rejected.
- [ ] `0xFF` is rejected.
- [ ] Output remains unchanged after rejection.

## `message_type_to_wire()`

- [ ] Every retained member maps to the expected byte.
- [ ] Encoding paths never call the helper with an invalid cast value.

## `message_type_to_string()`

- [ ] Every retained member returns the expected diagnostic string.
- [ ] An invalid cast value returns `"Unknown"`.

## Dispatch integration

- [ ] Every retained protocol type reaches its dedicated handler.
- [ ] Unknown protocol type is rejected under the protocol-kind model.
- [ ] Application-defined type is accepted only under the application-type model.
- [ ] ACK never reaches data dispatch.
- [ ] Handshake never reaches data dispatch.
- [ ] Log never reaches data dispatch.
- [ ] `Cmd` follows its defined behavior if retained.
- [ ] `Done` follows its defined behavior if retained.
- [ ] Reserved values are rejected by `sendData()`.

## Compatibility tests

- [ ] Legacy `'R'` peer and current `'C'` peer do not silently claim compatibility.
- [ ] Compatible peers agree on every reserved type byte.
- [ ] Wire-identifier changes require an intentional version change.
- [ ] Legacy migration behavior is covered explicitly.

## Reservation tests

- [ ] Every internal byte is recognized as reserved.
- [ ] Every internal byte is rejected by the application send path.
- [ ] Allowed application bytes remain usable under the application-type model.
- [ ] Sender and receiver reservation helpers produce identical decisions.

---

# Implementation checklist

## Protocol model

- [ ] Decide whether the packet-header type is a protocol frame kind or application message identifier.
- [ ] Separate protocol kinds from application message types.
- [ ] Define the complete reserved byte set.
- [ ] Document the selected model.

## Sending

- [ ] Reject reserved bytes in `sendData()`.
- [ ] Return a visible rejection result.
- [ ] Use the canonical reservation helper.
- [ ] Prevent invalid `MessageType` objects from entering internal encoding.

## Receiving

- [ ] Classify frame type before validating state.
- [ ] Use `message_type_from_wire()` under the protocol-kind model.
- [ ] Route every internal type to its internal handler.
- [ ] Never let an unexpected internal type become application data.
- [ ] Define unknown-type behavior.

## Declared types

- [ ] Implement or remove `Cmd`.
- [ ] Implement or remove `Done`.
- [ ] Move application conventions out of the protocol enum.
- [ ] Add payload and state documentation for every retained type.

## Compatibility

- [ ] Tie the `'R'` to `'C'` change to protocol-version handling.
- [ ] Define any temporary legacy acceptance period.
- [ ] Add old/new peer compatibility tests.

## Contract verification

- [ ] Add `sizeof(MessageType) == 1` assertion.
- [ ] Add fixed wire-value assertions or tests.
- [ ] Add duplicate-value tests.
- [ ] Document `message_type_to_wire()` preconditions.
- [ ] Test that failed raw conversion preserves output.

## Documentation

- [ ] Add `@file protocol/MessageType.h`.
- [ ] State whether the enum covers all types or only reserved internal types.
- [ ] List reserved byte values.
- [ ] Keep `message_type_to_string()` documented as diagnostic-only.

---

# Definition of done

The `MessageType.h` remediation is complete when:

- [ ] The packet-header type byte has one documented meaning.
- [ ] Protocol kinds and application identifiers are distinct concepts.
- [ ] Application sends cannot inject internal control frames.
- [ ] Every retained internal type has dedicated, documented handling.
- [ ] Unknown and unexpected internal types cannot become application data.
- [ ] `Cmd` and `Done` are implemented, removed, or reclassified deliberately.
- [ ] Legacy `'R'` and current `'C'` compatibility is versioned or otherwise managed.
- [ ] Enum size and wire values are locked down.
- [ ] Raw conversion preserves output on failure.
- [ ] Conversion, dispatch, reservation, and compatibility tests pass.

---

# Verdict

`MessageType.h` is well implemented as an enum-and-conversion utility. Its helpers are small, safe, and appropriate for embedded use.

The flaw is in the wider protocol model. TinyLink currently uses the packet-header type byte both as:

```text
a protocol control-frame kind
```

and:

```text
an arbitrary application message identifier
```

That ambiguity allows application sends to collide with internal control frames and causes undefined or unimplemented values to fall through into ordinary data dispatch.

Do not rewrite the enum mechanically yet. First decide the meaning of the packet-header type byte. Then align `MessageType`, `sendData()`, and `onFrameBoundary()` with that single model.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`
