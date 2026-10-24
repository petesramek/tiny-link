# Review of `src/internal/protocol/LogMessage.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/LogMessage.h`

The file defines a compact fixed-size log payload, but it contains a confirmed information-disclosure defect. `TinyLink::sendLog()` transmits the complete 32-byte structure, while `logmessage_set_text()` initializes only the used portion of the text array. The remaining bytes can contain uninitialized stack data.

---

# Summary

## Main findings

1. Short and null log strings leave unused text bytes uninitialized and transmit them over the wire.
2. `strlen()` scans without the destination's 24-character bound.
3. Timestamp and code fields use native byte order.
4. Packed C++ object representation is being used as the wire format.
5. Raw log-level values are not validated during decoding.
6. The internal `LogMessage` type is exposed through the public callback API.
7. Documentation encourages unsafe direct casting from raw bytes.
8. Received text is not guaranteed to be terminated.
9. The timestamp is sender-local uptime, not a universal timestamp.
10. `sendLog()` currently overwrites shared sequence state in `TinyLink.h`.

---

# P0: Prevent transmission of uninitialized memory

## 1. Initialize the complete text field

### Current behavior

The helper initializes only the used part of `LogMessage::text`:

```cpp
inline void logmessage_set_text(LogMessage& msg, const char* text) {
    if (!text) {
        msg.text[0] = '\0';
        return;
    }

    size_t len = strlen(text);

    if (len >= LOG_TEXT_CAPACITY) {
        len = LOG_TEXT_CAPACITY - 1;
    }

    memcpy(msg.text, text, len);
    msg.text[len] = '\0';
}
```

`TinyLink::sendLog()` creates the structure without value initialization:

```cpp
LogMessage msg;
```

It initializes `ts`, `level`, and `code`, then calls `logmessage_set_text()`. The complete structure is subsequently transmitted:

```cpp
send_internal(
    message_type_to_wire(MessageType::Log),
    _currSeq,
    reinterpret_cast<const uint8_t*>(&msg),
    sizeof(msg),
    false);
```

For a short string such as `"OK"`, only these text bytes are initialized:

```text
'O' 'K' '\0'
```

The remaining 22 bytes are uninitialized. For null text, only `text[0]` is written, leaving 24 uninitialized bytes.

The fixed 32-byte payload can therefore expose unrelated stack contents.

### Required change

Initialize the complete text field before copying:

```cpp
inline void logmessage_set_text(LogMessage& msg, const char* text)
{
    memset(msg.text, 0, sizeof(msg.text));

    if (!text) {
        return;
    }

    size_t len = 0;

    while (len < sizeof(msg.text) - 1 && text[len] != '\0') {
        ++len;
    }

    memcpy(msg.text, text, len);
}
```

Also value-initialize the entire outgoing message:

```cpp
LogMessage msg{};
```

### Recommendation

Apply both corrections:

- value-initialize the complete message in `sendLog()`
- make `logmessage_set_text()` independently initialize the full text array

This provides deterministic wire output and protects future callers that use the helper with a noninitialized message.

### Acceptance criteria

- Every byte of an outgoing log message is initialized before transmission.
- Null text produces 25 zero text bytes.
- Empty text produces 25 zero text bytes.
- Short text is followed entirely by zero bytes.
- Maximum text contains 24 characters and one terminating zero.
- Identical input produces identical 32-byte payloads.
- Unused text bytes cannot expose stack contents.

### Required tests

- Null text.
- Empty text.
- One-character text.
- Short text after a previously longer message.
- Twenty-three characters.
- Twenty-four characters.
- Twenty-five characters.
- Much longer text.
- Entire outgoing payload initialized with a known stack poison pattern before construction.
- Verify that no poison bytes remain in the encoded payload.

---

# P0: Bound source-string scanning

## 2. Replace unbounded `strlen()`

### Current behavior

The helper uses:

```cpp
size_t len = strlen(text);
```

The destination needs at most 24 source characters, but `strlen()` scans until a null terminator is found irrespective of the destination limit.

If the source is nonterminated, corrupted, or points near the end of valid memory, the scan can read beyond accessible memory. On embedded targets, it can also cause excessive execution time or a watchdog reset.

### Required change

Scan no more than the maximum number of characters that can be copied:

```cpp
size_t len = 0;

while (len < LOG_TEXT_MAX_LENGTH && text[len] != '\0') {
    ++len;
}
```

Then copy only that bounded range.

Do not rely on `strnlen()` unless it is available consistently across every supported target and toolchain.

### Acceptance criteria

- At most 24 source bytes are inspected.
- A nonterminated 24-byte source is safely copied and terminated in the destination.
- Runtime is bounded by `LOG_TEXT_MAX_LENGTH`.
- No unbounded `strlen()` remains in the log-message path.

### Required tests

- Null source.
- Empty string.
- One character.
- Exactly 23 characters.
- Exactly 24 characters.
- Exactly 25 characters.
- Longer string.
- A 24-byte source buffer without a null terminator.
- A larger nonterminated source buffer.

---

# P0: Define a portable wire representation

## 3. Encode multibyte fields with explicit byte order

### Current behavior

The structure contains:

```cpp
uint32_t ts;
uint16_t code;
```

The complete object representation is transmitted directly. The wire order of these fields therefore follows the sender's native byte order.

This will usually be little-endian on current TinyLink targets, but it is not defined by the protocol.

### Required change

Choose and document one wire byte order. Little-endian is the least disruptive choice if compatibility with current common targets matters.

Encode fields explicitly:

```cpp
output[0] = static_cast<uint8_t>(timestamp);
output[1] = static_cast<uint8_t>(timestamp >> 8);
output[2] = static_cast<uint8_t>(timestamp >> 16);
output[3] = static_cast<uint8_t>(timestamp >> 24);

output[4] = static_cast<uint8_t>(level);

output[5] = static_cast<uint8_t>(code);
output[6] = static_cast<uint8_t>(code >> 8);
```

The text field then occupies bytes 7 through 31.

### Acceptance criteria

- Timestamp byte order is documented.
- Code byte order is documented.
- Encoding does not depend on host endianness.
- Decoding reconstructs identical values on all supported architectures.
- Golden vectors verify exact field bytes and offsets.

### Required tests

- Timestamp `0x00000000`.
- Timestamp `0x01020304`.
- Timestamp `UINT32_MAX`.
- Code `0x0000`.
- Code `0x0102`.
- Code `UINT16_MAX`.
- Same vectors on little-endian and simulated big-endian decoding.

---

# P1: Stop using packed object representation as the wire format

## 4. Introduce explicit log encoding and decoding

### Current behavior

The packed structure and size assertion define the wire layout:

```cpp
TINYLINK_PACKED_BEGIN
struct LogMessage {
    uint32_t ts;
    uint8_t  level;
    uint16_t code;
    char     text[LOG_TEXT_CAPACITY];
} TINYLINK_PACKED;
TINYLINK_PACKED_END

static_assert(sizeof(LogMessage) == 32,
              "LogMessage must be exactly 32 bytes");
```

The size assertion is useful while direct structure serialization remains, but the approach still depends on:

- packing macro correctness
- host byte order
- compiler field layout
- integer object representation
- complete initialization of every transmitted byte

### Required change

Define the wire format independently from the semantic C++ type:

```text
Offset  Size  Field
0       4     sender uptime in milliseconds, little-endian
4       1     log level
5       2     application code, little-endian
7       25    zero-padded text
Total   32
```

Introduce explicit encode and decode helpers.

Suggested constants:

```cpp
static const size_t LOG_MESSAGE_WIRE_SIZE = 32;
static const size_t LOG_TEXT_CAPACITY = 25;
static const size_t LOG_TEXT_MAX_LENGTH = LOG_TEXT_CAPACITY - 1;
```

The semantic `LogMessage` may remain a structure, but its `sizeof` value must not define protocol compatibility.

### Acceptance criteria

- Wire size is defined independently of `sizeof(LogMessage)`.
- Every field offset is explicit.
- No `reinterpret_cast` is needed to transmit a log message.
- No wire payload is copied directly into a `LogMessage` object.
- Packing is not required for interoperability.
- Semantic output is committed only after complete validation.

### Required tests

- Fixed 32-byte golden vectors.
- Exact output capacity.
- Output capacity one byte too small.
- Exact input length.
- Input one byte too short.
- Input one byte too long.
- Output remains unchanged after decode failure.

---

# P1: Validate log levels

## 5. Reject or explicitly preserve unknown raw level values

### Current behavior

The wire structure stores severity as:

```cpp
uint8_t level;
```

Sending converts from `LogLevel`:

```cpp
msg.level = static_cast<uint8_t>(level);
```

Receiving copies the raw byte and invokes the callback without checking whether the value represents a declared `LogLevel`.

### Required decision

Choose one policy.

#### Reject unknown levels

Validate the raw byte against all declared `LogLevel` values before constructing the semantic message.

#### Preserve unknown levels

If forward-compatible unknown levels are required, preserve the raw byte separately instead of pretending every value is a declared enum member.

### Recommendation

Reject unknown levels until the protocol defines version or capability negotiation that makes future values safe to interpret.

### Acceptance criteria

- `LogLevel` has explicit and stable numeric values.
- The decoder defines how unknown values are handled.
- Invalid levels do not silently enter normal callback handling.
- Callback code cannot accidentally treat an unknown raw value as a known severity.

### Required tests

- Every declared log level.
- Lowest valid level.
- Highest valid level.
- Values immediately outside the valid range.
- Representative invalid values such as `0x7F` and `0xFF` where undefined.
- Invalid level does not invoke a successful log callback.

---

# P1: Separate public semantic type from internal wire format

## 6. Remove the internal wire type from the public callback

### Current behavior

The header says:

> Internal header: not part of the public API surface.

But `TinyLink.h` exposes:

```cpp
typedef void (*LogCallback)(const LogMessage& msg);
```

This makes `LogMessage` part of the public source API. Consumers depend on its type name, fields, text capacity, and header availability.

### Required change

Create a public semantic log type independent of the internal wire codec. For example:

```cpp
struct LogMessage {
    uint32_t uptimeMs;
    LogLevel level;
    uint16_t code;
    char text[LOG_TEXT_CAPACITY];
};
```

The public type should not be packed and should not serialize itself through object representation.

The internal codec should translate between the fixed 32-byte wire payload and this semantic value.

### Acceptance criteria

- Public callbacks use a public semantic type.
- Consumers need only public headers.
- The public type is not required to be 32 bytes.
- Wire-layout changes do not automatically break the public callback contract.
- Callbacks receive only validated and normalized values.

### Required tests

- A consumer registers the callback using public headers only.
- Internal wire codecs can change without changing the callback signature.
- Callback text is always terminated.
- Callback level is always valid according to the chosen policy.

---

# P1: Remove unsafe direct-casting guidance

## 7. Do not cast raw payload bytes to `LogMessage`

### Current behavior

The header states:

> Receivers can cast the raw payload directly to LogMessage to access all fields.

This is unsafe or nonportable because:

- the raw address may not satisfy normal object alignment
- packed multibyte members may be unaligned
- the fields use native byte order
- raw level values are not validated
- text termination cannot be trusted from an arbitrary peer
- object-lifetime rules vary across supported C++ standards
- layout depends on packing macros

### Required change

Remove the direct-casting recommendation. Require receivers to use the explicit decoder.

The decoder must:

1. validate exact payload length
2. decode integer byte order
3. validate log level
4. copy and normalize text
5. guarantee termination
6. commit semantic output only after success

### Acceptance criteria

- Documentation does not encourage casting raw bytes to C++ structures.
- All repository callers use the decoder.
- No callback is invoked before validation completes.

---

# P1: Guarantee safe received text

## 8. Normalize or reject nonterminated wire text

### Current behavior

The local sender intends to terminate outgoing text. A malformed, incompatible, or third-party peer can still send 25 nonzero text bytes.

The current receiver copies the entire payload into `LogMessage` and invokes the callback. If callback code treats `text` as a C string, it can read past the array.

### Required decision

Choose one policy.

#### Strict canonical policy

Reject a log payload whose text field contains no null byte.

#### Normalizing policy

Copy at most 24 characters and always set:

```cpp
message.text[LOG_TEXT_MAX_LENGTH] = '\0';
```

Optionally record that a noncanonical payload was normalized.

### Recommendation

Normalize incoming text and record a malformed or noncanonical-text statistic when appropriate. A diagnostic frame should not destabilize the protocol because a peer omitted a terminator.

Also define whether bytes after the first null must be zero. Requiring zero padding gives deterministic canonical encoding and prevents hidden trailing data.

### Acceptance criteria

- Every successful log callback receives terminated text.
- Twenty-five nonzero wire bytes cannot cause an out-of-bounds string read.
- Embedded null characters follow a documented policy.
- Nonzero bytes after the first null are either normalized, rejected, or explicitly permitted.

### Required tests

- Empty text field.
- Properly terminated short text with zero padding.
- Early null followed by nonzero bytes.
- Exactly 24 characters and a terminator.
- Twenty-five nonzero bytes.
- Embedded null character.
- Callback always observes a terminated array.

---

# P1: Clarify timestamp semantics

## 9. Rename or document `ts` as sender-local uptime

### Current behavior

The field is described as a timestamp:

```cpp
uint32_t ts; ///< Timestamp in milliseconds
```

`TinyLink::sendLog()` assigns:

```cpp
static_cast<uint32_t>(_hw->millis())
```

This is normally sender-local elapsed time since boot or adapter initialization. It is not:

- UTC time
- wall-clock time
- synchronized with the receiver
- globally comparable across devices

A 32-bit millisecond value wraps after approximately 49.7 days.

### Required change

Prefer a semantic name such as:

```cpp
uint32_t uptimeMs;
```

Document:

- the value's origin
- that it is sender-local
- that it wraps modulo `2^32`
- that it cannot be compared across peers without separate synchronization

### Acceptance criteria

- Consumers do not interpret the value as wall-clock time.
- Wraparound behavior is documented.
- Numeric encoding remains deterministic.
- Tests cover values around wraparound.

### Required tests

- Uptime zero.
- Uptime one.
- `UINT32_MAX - 1`.
- `UINT32_MAX`.
- Wrap to zero.

---

# P1: Preserve application sequence state when logging

## 10. `sendLog()` must not overwrite receive-sequence state

### Current behavior

The defect is implemented in `TinyLink.h`:

```cpp
_currSeq = _nextSeq++;
```

The same `_currSeq` field is also used as the last received application sequence and by `sendAck()`.

Sending a log can therefore cause a later ACK to acknowledge the wrong application frame.

### Required change

Use independent fields for:

- last received application sequence
- next transmitted sequence
- pending application ACK sequence

A log frame may consume a transmitted sequence if that is part of the protocol, but it must not alter receive state or pending data-ACK identity.

### Acceptance criteria

- Sending a log does not change `seq()`.
- Sending a log does not change the sequence echoed by `sendAck()`.
- Sending a log does not replace the sequence of a pending data ACK.
- Log sequence semantics are documented.
- Logs do not enter application ACK waiting unless explicitly designed to do so.

### Required tests

- Receive data, send log, then send ACK.
- Send data and await ACK, then send log.
- Send multiple logs while application ACK is pending.
- Verify that the pending application sequence remains unchanged.

---

# P2: Constants and API naming

## 11. Use `size_t` for capacity constants

### Current behavior

The text capacity is declared as:

```cpp
static const uint8_t LOG_TEXT_CAPACITY = 25;
```

The value is used as an array bound and in `size_t` length arithmetic.

### Required change

Use `size_t` for memory and buffer sizes:

```cpp
static const size_t LOG_TEXT_CAPACITY = 25;
```

Use the constant style supported by the project's C++ baseline.

### Acceptance criteria

- Buffer-size constants use the appropriate size type.
- Length arithmetic does not depend on unnecessary integer promotion.

---

## 12. Define maximum text length separately from capacity

### Current behavior

The text array capacity is 25 bytes, while the maximum stored string length is 24 characters.

### Required change

Represent both concepts explicitly:

```cpp
static const size_t LOG_TEXT_CAPACITY = 25;
static const size_t LOG_TEXT_MAX_LENGTH =
    LOG_TEXT_CAPACITY - 1;
```

### Acceptance criteria

- Copy loops use `LOG_TEXT_MAX_LENGTH`.
- Array declarations use `LOG_TEXT_CAPACITY`.
- Comments do not alternate ambiguously between capacity and text length.

---

## 13. Define application-code semantics

### Current behavior

The field is described as:

```cpp
Application error / event code
```

This is reasonable but incomplete for interoperability.

### Required documentation

Clarify:

- whether code zero is valid
- whether codes are application-defined and opaque to TinyLink
- whether code ranges have owners
- whether interpretation depends on level
- whether codes must remain stable across firmware versions

A centralized global registry is not required unless the project needs it. The protocol should simply avoid implying meanings that it does not enforce.

### Acceptance criteria

- The code field is documented as either opaque or governed by a defined registry.
- Zero-value semantics are clear.
- TinyLink does not validate application-owned codes unless explicitly designed to do so.

---

# P2: Documentation corrections

## 14. Correct the `@file` path

### Current behavior

The file declares:

```cpp
@file internal/LogMessage.h
```

The reviewed path is:

```text
src/internal/protocol/LogMessage.h
```

### Required change

Use either:

```cpp
@file LogMessage.h
```

or:

```cpp
@file internal/protocol/LogMessage.h
```

Apply the selected convention consistently.

---

## 15. Define the canonical wire format as bytes

### Current behavior

The header describes the packed structure as the canonical wire format.

### Required change

The canonical format must describe:

- byte offsets
- field sizes
- integer byte order
- valid level values
- text capacity
- termination and padding rules
- timestamp semantics
- exact payload length

The C++ semantic structure should be treated as one programming-language representation, not the protocol definition itself.

### Acceptance criteria

- Protocol documentation can be implemented by a peer that does not use C++.
- No compiler packing knowledge is required to implement the format.
- Golden vectors correspond directly to the documented byte layout.

---

# Positive findings

The following elements should be retained where applicable.

## Exact size assertion

```cpp
static_assert(sizeof(LogMessage) == 32,
              "LogMessage must be exactly 32 bytes");
```

This catches packing or field-layout drift while direct structure serialization remains.

## Fixed-width numeric fields

The structure uses `uint32_t`, `uint8_t`, and `uint16_t` rather than platform-sized `int` or `long` fields.

## Fixed text capacity

The log payload remains bounded and compatible with the TinyLink micro-message goal.

## Null-input intent

The helper intentionally treats null text as an empty string. The implementation must be corrected to initialize the complete field.

## Reserved terminator space

The selected 25-byte field supports 24 text characters and one guaranteed terminator, which is a sensible fixed-size policy.

---

# Required test suite

## Text handling

- [ ] Null text produces 25 zero bytes.
- [ ] Empty text produces 25 zero bytes.
- [ ] One-character text produces one character and 24 zero bytes.
- [ ] Twenty-three-character text.
- [ ] Twenty-four-character text.
- [ ] Twenty-five-character text is truncated to 24.
- [ ] Long text is truncated safely.
- [ ] Nonterminated 24-byte input is handled safely.
- [ ] A longer nonterminated source does not cause unbounded scanning.
- [ ] Repeated calls do not preserve bytes from earlier content.
- [ ] Entire wire payload is deterministic.

## Golden wire vectors

- [ ] Timestamp zero.
- [ ] Timestamp `0x01020304`.
- [ ] Each valid log level.
- [ ] Code zero.
- [ ] Code `0x0102`.
- [ ] Empty text.
- [ ] Maximum text.
- [ ] Fixed expected 32-byte output for representative complete messages.

Expected vectors must not be generated by copying a `LogMessage` object.

## Decode validation

- [ ] Exact 32-byte payload.
- [ ] Payload one byte short.
- [ ] Payload one byte long.
- [ ] Invalid log level.
- [ ] Text with no terminator.
- [ ] Text with early terminator and zero padding.
- [ ] Text with early terminator and nonzero trailing bytes.
- [ ] Timestamp and code decode correctly.
- [ ] Semantic output remains unchanged after failure.

## Public API boundary

- [ ] Public callback uses public headers only.
- [ ] Public semantic type does not require packing.
- [ ] Internal wire-layout changes do not automatically change the callback contract.
- [ ] Callback receives only validated values.

## Sequence integration

- [ ] Sending a log does not change the last received sequence.
- [ ] Sending a log does not change the pending application ACK identity.
- [ ] Logging while awaiting a data ACK follows the documented policy.
- [ ] Sending a log before `sendAck()` does not change the acknowledged sequence.

## Time behavior

- [ ] Uptime zero.
- [ ] Uptime near `UINT32_MAX`.
- [ ] Uptime `UINT32_MAX`.
- [ ] Wrap to zero.
- [ ] Documentation and test names treat the value as sender-local uptime.

## Information-disclosure regression

- [ ] Construct the message in memory prefilled with `0xAA`.
- [ ] Set short text.
- [ ] Encode the complete message.
- [ ] Verify no `0xAA` values remain in unused text bytes unless they are legitimate text data.
- [ ] Repeat with null and empty text.
- [ ] Run under MemorySanitizer where available.

---

# Dependency review required

## `src/protocol/LogLevel.h`

Review this file next to confirm:

- explicit underlying type
- stable numeric wire values
- contiguous or noncontiguous value policy
- handling of unknown raw values
- whether all declared levels should be accepted from the wire
- whether aliases or reserved values exist

## `src/internal/protocol/Packed.h`

Review packing behavior while any other wire structure still uses it:

- GCC and Clang attributes
- AVR behavior
- MSVC push/pop symmetry
- packing-state restoration
- nested include safety
- absence of packing leakage

---

# Implementation checklist

## Immediate data-safety fixes

- [ ] Change outgoing construction to `LogMessage msg{};`.
- [ ] Zero the complete text field inside `logmessage_set_text()`.
- [ ] Replace `strlen()` with bounded scanning.
- [ ] Add a regression test for uninitialized-byte disclosure.

## Wire format

- [ ] Define `LOG_MESSAGE_WIRE_SIZE` independently of `sizeof`.
- [ ] Define field offsets and byte order.
- [ ] Add explicit encode and decode helpers.
- [ ] Stop serializing object representation.
- [ ] Stop copying raw wire bytes directly into the semantic object.
- [ ] Add fixed 32-byte golden vectors.

## Validation

- [ ] Validate exact payload length.
- [ ] Validate raw log-level values.
- [ ] Guarantee terminated text after decoding.
- [ ] Define handling of nonzero bytes after an early terminator.
- [ ] Leave semantic output unchanged after decode failure.

## Public API

- [ ] Move the semantic log type to the public protocol surface.
- [ ] Keep wire encoding internal.
- [ ] Remove the direct raw-casting recommendation.
- [ ] Ensure consumers need only public headers.

## Semantics

- [ ] Rename `ts` to `uptimeMs` or document it precisely.
- [ ] Document 32-bit millisecond wraparound.
- [ ] Document application-code ownership and zero semantics.
- [ ] Ensure logging does not modify receive-sequence or pending-ACK state.

## Constants and documentation

- [ ] Change text capacity constants to `size_t`.
- [ ] Add `LOG_TEXT_MAX_LENGTH`.
- [ ] Correct the `@file` path.
- [ ] Replace object-layout documentation with byte-layout documentation.
- [ ] Review `src/protocol/LogLevel.h`.
- [ ] Review `src/internal/protocol/Packed.h`.

---

# Definition of done

The `LogMessage.h` remediation is complete when:

- [ ] Every byte of every outgoing log payload is initialized.
- [ ] Source scanning is bounded to the maximum copied text length.
- [ ] Timestamp and code byte order are explicit.
- [ ] Wire encoding and decoding do not depend on packed object representation.
- [ ] Unknown log levels follow a documented policy.
- [ ] Every successful decoded log contains terminated text.
- [ ] Public callbacks use a public semantic type rather than the internal wire type.
- [ ] Documentation no longer recommends raw casting.
- [ ] Timestamp semantics are clearly sender-local and wrap-aware.
- [ ] Sending logs cannot alter application receive sequence or pending ACK identity.
- [ ] Golden-vector, disclosure, boundary, validation, API, sequence, and wraparound tests pass.

---

# Verdict

`LogMessage.h` contains the most concrete data-exposure defect found in the internal message structures so far.

Because `LogMessage msg;` is not initialized and `logmessage_set_text()` writes only through the first terminator, every short or null log message can transmit unused stack bytes in the fixed 25-byte text field.

The immediate correction is:

```cpp
LogMessage msg{};
```

combined with a bounded helper that initializes all 25 text bytes.

The durable correction is explicit 32-byte encoding and decoding with defined byte order, validated log levels, normalized terminated text, and separation between the public semantic message and the internal wire representation.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/LogLevel.h`
