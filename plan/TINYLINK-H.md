# TinyLink Remediation Plan

## Scope

This document records the defects, design risks, and verification work identified during the review of `src/TinyLink.h`.

The findings are divided into:

- **Confirmed defects** visible directly in `TinyLink.h`
- **Contract-dependent risks** that require review of `Packet.h`, `CobsCodec.h`, protocol message definitions, and adapter implementations
- **Hardening work** needed before TinyLink should be treated as a reliable production protocol

The correction order is intentional. Protocol correctness should be fixed before structural refactoring or API polish.

---

## Release recommendation

TinyLink should not currently be considered production-ready for reliable, concurrent, interrupt-driven, cross-platform, or safety-relevant communication.

The most important blockers are:

1. ACK frames are not correlated with the transmitted frame they acknowledge.
2. Another data frame can be sent while an ACK is already pending.
3. A single sequence field is used for unrelated receive and transmit responsibilities.
4. Unexpected internal frames can fall through into application-data dispatch.
5. RX overflow does not discard the remainder of the oversized frame.
6. The documented direct ISR execution model is unsafe.
7. Raw native struct serialization does not define a portable wire format.

---

# P0: Protocol correctness blockers

## 1. Separate receive, transmit, and pending-ACK sequence state

### Problem

`_currSeq` currently has several incompatible meanings:

- the sequence assigned by `sendData()`
- the sequence assigned by `sendLog()`
- the sequence extracted by `dispatchDataFrame()`
- the sequence used by `sendAck()`
- the value exposed by `seq()` as the most recently received sequence

A transmitted data or log frame can overwrite the sequence needed to acknowledge the last received data frame.

### Required change

Replace `_currSeq` with independent state representing at least:

- the sequence of the last successfully received application data frame
- the next sequence assigned to a transmitted frame
- the sequence of the transmitted data frame currently awaiting an ACK
- whether an ACK is currently pending

Suggested responsibilities:

```cpp
uint8_t _lastReceivedSeq;
uint8_t _nextTransmitSeq;
uint8_t _pendingAckSeq;
bool _hasPendingAck;
```

The exact names may differ, but these responsibilities must not share one field.

### Acceptance criteria

- Sending a log cannot change the sequence used by `sendAck()`.
- Sending another protocol message cannot change `seq()`.
- `seq()` returns only the sequence of the most recently accepted application data frame.
- ACK correlation uses only the pending transmitted-data sequence.
- Sequence wraparound from `255` to `0` remains valid.

### Required tests

- Receive data sequence 20, send a log, call `sendAck()`, and verify that sequence 20 is acknowledged.
- Receive data sequence 20, send data sequence 5, call `sendAck()`, and verify that sequence 20 is acknowledged.
- Verify that local sends do not change `seq()`.
- Verify wraparound at sequence 255.

---

## 2. Validate ACK sequence before clearing pending state

### Problem

`dispatchAckFrame()` accepts any structurally valid `TinyAck` while the engine is in `AWAITING_ACK`. It does not compare `ack.seq` with the sequence of the frame currently awaiting acknowledgement.

A delayed, duplicated, stale, unsolicited, or unrelated ACK can therefore release the sender from `AWAITING_ACK`.

The envelope sequence returned as `rseq` is also ignored.

### Required change

- Store the sequence of the outstanding transmitted data frame.
- Accept an ACK only when its acknowledged sequence matches that pending sequence.
- Define whether the ACK envelope sequence must also match the payload sequence.
- Treat a mismatching ACK as an unexpected protocol event without clearing the pending state.
- Define how `TinyAck.result` affects the sender state and callback behavior.

The protocol should preferably carry the acknowledged sequence only once. If it remains in both the envelope and payload, their relationship must be specified and validated.

### Acceptance criteria

- A matching ACK clears the pending state.
- A stale ACK does not clear the pending state.
- A future or unrelated ACK does not clear the pending state.
- A duplicate ACK received after completion is ignored or reported as unexpected.
- An invalid ACK never becomes application data.
- ACK result handling is documented and tested.

### Required tests

- Matching ACK.
- Stale ACK.
- Future ACK.
- Duplicate ACK.
- Unsolicited ACK.
- ACK with inconsistent envelope and payload sequences.
- ACK with a non-success result.

---

## 3. Enforce one outstanding data frame in stop-and-wait mode

### Problem

`sendData()` is allowed while `_state == TinyState::AWAITING_ACK` because `connected()` returns true for every handshake-enabled state except `CONNECTING`.

A second send overwrites the ACK timeout timestamp and any pending-send identity. An ACK for the first frame can then release the second frame.

### Required change

Adopt an explicit stop-and-wait invariant:

- At most one transmitted application data frame may await an ACK.
- `sendData()` must reject a new send while an ACK is pending.
- Connection state and send readiness must be separate concepts.

If multiple outstanding frames are genuinely required, replace the current single-state implementation with a bounded pending-frame collection and per-frame timeout tracking. That is not recommended for TinyLink's current scope.

### Acceptance criteria

- A second data send is rejected while an ACK is pending.
- Rejection is visible to the caller.
- Logs and internal protocol responses follow an explicitly documented concurrency policy.
- Receipt or timeout of the pending ACK restores send readiness according to the protocol contract.

### Required tests

- Second send before ACK.
- Send immediately after matching ACK.
- Send immediately after ACK timeout.
- Send after stale ACK.
- Send application response from a receive callback while another local ACK is pending.

---

## 4. Make `sendData()` report failure

### Problem

`sendData()` returns `void` and silently drops a send when:

- the adapter is closed
- handshake mode is still connecting
- future stop-and-wait rules reject a send
- packing fails
- encoding fails
- writing fails

The caller cannot determine whether a frame was transmitted.

### Required change

Return a defined send result. A typed result is preferable to `bool` because callers need to distinguish protocol-state rejection from encoding or transport failure.

Suggested categories:

```cpp
enum class SendResult {
    Sent,
    PortClosed,
    NotConnected,
    AckPending,
    PayloadRejected,
    EncodingFailed,
    WriteFailed
};
```

### Acceptance criteria

- Every non-transmission path returns a specific result.
- State changes occur only after the complete frame has been accepted by the adapter contract.
- Statistics correspond to the actual failure category.

---

## 5. Classify reserved wire types before state validation

### Problem

An ACK is dispatched as an ACK only when the prior state is `AWAITING_ACK`. Otherwise it falls through into `dispatchDataFrame()`.

If the application payload size matches an internal payload size, an unexpected ACK or another reserved frame can potentially be accepted as application data.

### Required change

Perform message classification independently from state validation:

1. Determine whether the wire type is handshake, ACK, log, application data, or unknown/reserved.
2. Route every internal type to its internal handler.
3. Let that handler validate whether the message is legal in the current state.
4. Never reinterpret a reserved internal frame as application data.

### Acceptance criteria

- ACK frames never reach `onDataReceived()` or the polling mailbox.
- Handshake frames never reach application-data dispatch.
- Log frames never reach application-data dispatch.
- Unknown reserved types are rejected through a defined path.
- Internal-frame validation does not depend on callback registration.

### Required tests

- Unsolicited ACK when idle.
- ACK during connection.
- ACK when `sizeof(T) == sizeof(TinyAck)`.
- Malformed handshake with `sizeof(T) == sizeof(HandshakeMessage)`.
- Log frame with no log callback registered.
- Unknown reserved message type.

---

## 6. Prevent application message types from colliding with internal types

### Problem

`sendData(uint8_t type, ...)` accepts any byte, including wire values reserved for handshake, ACK, and log messages. The receiving peer will classify such a frame as internal protocol traffic.

### Required change

- Define and document the internal and application wire-type ranges.
- Reject reserved internal values in `sendData()`.
- Prefer an application message-type abstraction that cannot represent internal values.

### Acceptance criteria

- Application code cannot transmit a data frame using a reserved internal type.
- A rejected type is reported to the caller.
- Both encoding and decoding enforce the same reservation rules.

---

## 7. Discard an entire oversized frame after RX overflow

### Problem

When `_rawBuf` overflows, `_rawIdx` is reset to zero, but the remainder of the same oversized frame is immediately accumulated as a new frame. Its suffix can therefore be decoded at the next delimiter.

### Required change

Add an explicit discard-until-delimiter state or flag.

When overflow occurs:

- increment the overflow statistic once
- reset the accumulation index
- ignore all subsequent nonzero bytes
- leave discard mode only when the next `0x00` delimiter arrives
- then resume normal synchronization

### Acceptance criteria

- No suffix of an oversized frame is decoded or dispatched.
- The first valid frame after the oversized frame is accepted.
- Overflow is counted once per discarded frame, not once per subsequent byte.

### Required tests

- Exactly maximum encoded frame size.
- Maximum plus one byte.
- Much larger frame.
- Oversized frame whose suffix is itself valid COBS and contains a valid checksum.
- Valid frame immediately following the oversized frame.

---

## 8. Validate handshake discriminator and protocol compatibility

### Problem

The implementation documents only handshake values `0` and `1`, but every value moves the engine out of `CONNECTING`.

The field named `version` is also being used as a request/reply discriminator rather than clearly representing protocol compatibility.

### Required change

- Reject values other than the defined handshake kinds.
- Do not establish a connection after an invalid handshake.
- Separate request/reply kind from protocol version if version negotiation is required.
- Define behavior for unsupported future versions.

### Acceptance criteria

- Value `0` is handled as an initial request.
- Value `1` is handled as a reply.
- Unsupported values do not connect the link.
- Unsupported values are counted or surfaced diagnostically.
- Simultaneous handshake initiation remains valid.

---

# P1: Execution, lifetime, and transport safety

## 9. Remove direct full-engine execution from ISR context

### Problem

`autoUpdateISR()` calls the complete `update()` pipeline. That pipeline can:

- call adapter I/O and time functions
- decode COBS
- unpack and validate packets
- copy payloads
- send handshake responses
- invoke arbitrary user callbacks
- allow callbacks to send ACKs, logs, or data
- use substantial stack space

The class also shares mutable state between interrupt and foreground execution without atomic operations or critical sections.

The implementation therefore cannot generally be described as ISR-safe.

### Required change

Use one of these models:

1. **Preferred:** ISR receives bytes into a bounded ring buffer and foreground code runs `update()` to parse and dispatch them.
2. ISR sets a lightweight work-pending flag and foreground code performs all protocol work.
3. Explicitly support only polling or task-context execution.

Remove or rewrite documentation that encourages calling the complete engine from arbitrary timer or UART interrupt contexts.

### Acceptance criteria

- User callbacks never execute in hardware interrupt context unless a separate, deliberately constrained API is introduced.
- Adapter operations used by protocol parsing execute in a documented context.
- Foreground and interrupt code do not race over protocol state.
- Worst-case ISR duration is bounded and minimal.

### Required tests and verification

- Stress RX while transmitting from foreground code.
- Verify no state corruption under the chosen execution model.
- Measure worst-case interrupt duration if any ISR component remains.
- Verify ring-buffer overflow behavior if a queue is introduced.

---

## 10. Remove unsafe implicit static-instance registration

### Problem

The constructor assigns `_s_instance = this`. A later instance of the same template specialization silently replaces the earlier target. The pointer is not cleared when an instance is destroyed, so `autoUpdateISR()` can call a dangling pointer.

### Required change

- Prefer explicit registration instead of constructor side effects.
- Define ownership and lifetime requirements.
- Ensure unregistration cannot leave a dangling pointer.
- If automatic registration remains, provide deterministic destruction and replacement behavior.

### Acceptance criteria

- Destroying a registered instance cannot leave an accessible invalid pointer.
- Constructing an unrelated instance does not silently reroute updates unless explicitly documented and requested.
- Multiple instances have deterministic behavior.

---

## 11. Define and enforce the adapter write contract

### Problem

Encoding and send paths ignore:

- encoder failure
- partial writes
- write failures
- failure of delimiter writes
- temporary transport backpressure

The current SFINAE validation checks only that write expressions exist. It does not validate return types or semantics.

### Required change

Define a precise adapter contract, for example:

```cpp
bool write(uint8_t value);
bool writeAll(const uint8_t* data, size_t length);
```

or:

```cpp
size_t write(const uint8_t* data, size_t length);
```

with explicit handling of partial writes.

The complete encoded frame should be treated as sent only after all bytes, including delimiters, have been accepted according to that contract.

### Acceptance criteria

- `cobs_encode()` failure is detected before writing.
- Partial writes are handled or reported.
- Failed opening delimiter, body, or closing delimiter is reported.
- Send state and ACK timers start only according to the defined successful-send boundary.
- Adapter tests verify the exact contract.

---

## 12. Reduce and measure stack usage

### Problem

`send_internal()` allocates approximately 142 bytes of frame buffers at the protocol maximum, excluding call frames and callback stack use. A send from a receive callback produces a deeper call chain. Direct ISR use makes this more dangerous on small AVR targets.

### Required change

- Measure worst-case stack use on every supported constrained target.
- Remove direct parsing and callbacks from ISR context.
- Decide whether TX scratch buffers should be instance-owned, externally provided, or retained on the stack.
- Document reentrancy implications of the chosen storage model.

### Acceptance criteria

- Worst-case stack consumption is recorded for supported AVR targets.
- Stack headroom is demonstrated under nested receive-callback-send behavior.
- Buffer placement does not reintroduce RX/TX aliasing.

---

# P1: State-machine and liveness corrections

## 13. Decouple ACK timeout from RX accumulation state

### Problem

ACK timeout is evaluated only when `_rawIdx == 0`. Continuous or repeated partial input can therefore postpone ACK timeout handling.

### Required change

Evaluate the ACK deadline independently of whether an unrelated receive frame is partially accumulated.

Define ordering for the boundary case where an ACK is already available at the exact timeout. For example:

- process available bytes before expiring the deadline, or
- expire first using a documented strict deadline policy.

### Acceptance criteria

- Partial inbound traffic cannot indefinitely postpone ACK timeout.
- ACK timeout behavior is deterministic at the deadline boundary.
- Inter-byte timeout and ACK timeout remain independent.

---

## 14. Decide whether unconsumed application data may suspend the protocol

### Problem

`update()` returns immediately while `_hasNew` is true. Until the application calls `flush()`:

- bytes are not consumed
- ACKs are not processed
- handshakes are not processed
- retries and timeouts stop
- logs are not processed
- adapter RX buffers may overflow

### Required change

Choose and document a policy:

- continue processing protocol frames while retaining one application-data mailbox
- use a fixed-size application-data ring buffer
- drop new application messages with statistics while continuing internal protocol processing
- make callback-only consumption mandatory

Suspending the entire protocol should not remain an accidental consequence of a one-slot polling API.

### Acceptance criteria

- Internal protocol maintenance continues according to the chosen policy.
- Application backpressure behavior is explicit.
- Message drops or queue overflow are measurable.
- An unconsumed application message cannot silently disable connection maintenance unless that is explicitly the protocol contract.

---

## 15. Define handshake retry limits and connection failure behavior

### Problem

Handshake retry continues forever with a fixed one-second interval. There is no failed state, maximum duration, retry count, backoff, or failure callback.

### Required change

Define whether infinite retry is intentional. If not, add configurable:

- retry interval
- maximum retry count or maximum connection duration
- connection-failed outcome
- retry and failure statistics

If infinite retry remains the default, expose enough state for the application to detect and manage it.

### Acceptance criteria

- Connection retry policy is documented.
- Application code can distinguish connecting, connected, and failed or abandoned connection states.
- Retry attempts are observable.

---

## 16. Distinguish handshake attempts from successful sends

### Problem

`_lastHandshakeSent` is updated before checking whether `send_internal()` succeeds. The name implies successful transmission, while the behavior represents the most recent attempt.

### Required change

Either:

- rename the field and explicitly track attempt time, or
- update it only after successful transmission.

Define retry behavior after adapter write failure.

### Acceptance criteria

- Field naming matches behavior.
- A failed write follows a documented retry schedule.
- Failed handshake writes are counted distinctly.

---

## 17. Define what ACK support guarantees

### Problem

An ACK timeout only leaves `AWAITING_ACK`; it does not retransmit the frame, retain the payload, or expose the timed-out sequence through a dedicated result. The reliability guarantee is therefore unclear.

### Required change

Define ACK semantics as one of:

- delivery confirmation only
- stop-and-wait reliability with retransmission
- flow control
- request-response synchronization

If retransmission is required, retain the encoded frame or payload, define retry limits, and address duplicate delivery. If retransmission is not required, document that ACKs do not guarantee delivery.

### Acceptance criteria

- ACK purpose is documented precisely.
- Timeout behavior follows that purpose.
- Duplicate and retransmitted data behavior is defined.
- Application code can identify the sequence and outcome of a timed-out send.

---

# P1: Wire-format correctness and portability

## 18. Stop treating `alignof(T) == 1` as serialization safety

### Problem

Alignment of one does not prove that `T` is safe to transmit as raw bytes. A type can still be non-trivially copyable, pointer-containing, padded, endian-dependent, or ABI-dependent.

### Required change

As an immediate guard on non-AVR builds, consider requiring:

```cpp
static_assert(std::is_trivially_copyable<T>::value,
              "TinyLink payload must be trivially copyable");
static_assert(std::is_standard_layout<T>::value,
              "TinyLink payload must have standard layout");
```

These checks are only interim safeguards. The robust correction is explicit field serialization with a documented wire representation.

### Acceptance criteria

- Non-trivially-copyable payloads are rejected at compile time where traits are available.
- Pointer-containing and otherwise unsupported payload designs are documented as invalid.
- Wire compatibility does not rely only on native object representation.

---

## 19. Define byte order for every multibyte field

### Problem

`uint16_t`, `uint32_t`, floating-point values, and other multibyte fields are currently transmitted in native object order.

### Required change

Specify one protocol byte order and encode/decode every multibyte field explicitly. Apply this to both application payload serialization and internal structures such as logs and acknowledgements.

### Acceptance criteria

- The protocol specification names the byte order.
- Golden-vector tests validate exact transmitted bytes.
- Peers built for different architectures produce and consume the same wire representation.

---

## 20. Remove compiler-dependent wire struct layouts

### Problem

The protocol assumes exact sizes for `TinyAck`, `HandshakeMessage`, and `LogMessage`, but compiler packing and ABI rules can differ.

### Required change

Short term:

```cpp
static_assert(sizeof(LogMessage) == 32, "Unexpected LogMessage layout");
static_assert(sizeof(TinyAck) == 2, "Unexpected TinyAck layout");
static_assert(sizeof(HandshakeMessage) == 1,
              "Unexpected HandshakeMessage layout");
```

Long term, encode and decode fields explicitly instead of copying complete structs.

### Acceptance criteria

- Exact internal wire sizes are verified at compile time.
- Golden packet tests verify field offsets and byte order.
- Compiler packing differences cannot silently create incompatible peers.

---

## 21. Define floating-point and Boolean payload representation

### Problem

Raw application struct transmission leaves floating-point format, Boolean size, enum representation, padding, and other ABI details undefined.

### Required change

The payload contract must either:

- forbid these types unless encoded explicitly, or
- define their exact wire representations.

### Acceptance criteria

- Supported primitive wire types are documented.
- Unsupported native types are rejected or clearly prohibited.
- Cross-platform golden-vector tests cover every supported wire primitive.

---

# P2: API and maintainability corrections

## 22. Separate connection status from send readiness

### Problem

`connected()` means hardware-open in basic mode and merely not-`CONNECTING` in handshake mode. It returns true while awaiting an ACK, even when stop-and-wait should prohibit another data send.

### Required change

Expose separate concepts, such as:

```cpp
bool connected() const;
bool canSend() const;
bool awaitingAck() const;
```

### Acceptance criteria

- `connected()` has one documented meaning.
- `canSend()` reflects adapter, handshake, and pending-ACK state.
- Callers do not need to infer send readiness from internal state values.

---

## 23. Preserve callbacks during protocol reset

### Problem

`reset()` clears callback registrations even though its documentation describes clearing protocol state and counters. Callbacks are configuration, not transient protocol state.

### Required change

- Preserve callbacks in protocol reset.
- Provide a distinct callback-clear operation if needed.
- Rename reset operations if multiple reset levels are necessary.

### Acceptance criteria

- Reset behavior matches documentation.
- Protocol recovery does not silently disable application handlers.
- Tests verify callbacks before and after reset.

---

## 24. Move internal state-machine helpers out of the public API

### Problem

Methods such as `tickHandshake()`, `tickTimeouts()`, `accumulateByte()`, dispatch helpers, and `onFrameBoundary()` are public and allow external callers to violate engine invariants.

### Required change

Move implementation helpers to `private`. If tests require seams, test through the public adapter interface or introduce a dedicated internal test-access mechanism.

### Acceptance criteria

- Public API exposes only supported application operations.
- External code cannot drive partial internal transitions directly.
- Tests continue to cover state behavior through supported entry points.

---

## 25. Make protocol validation independent of callbacks

### Problem

Log frames are unpacked only when a log callback exists. Some malformed internal frames are therefore not fully validated when no handler is registered.

### Required change

Always decode and validate each internal frame. Invoke callbacks only after successful validation.

### Acceptance criteria

- The same frame is considered valid or invalid regardless of callback registration.
- Invalid internal frames update consistent diagnostics.
- Callbacks receive only fully validated messages.

---

## 26. Use accurate diagnostic categories

### Problem

`ERR_CRC` is used for failures that may actually be local packing errors, payload-length mismatches, malformed frames, or other decode failures.

### Required change

Introduce diagnostic categories that preserve the actual failure cause, including as appropriate:

- packet pack failure
- COBS encoding failure
- COBS decoding failure
- checksum mismatch
- payload-size mismatch
- unexpected internal type
- unexpected sequence
- invalid handshake
- RX overflow
- inter-byte timeout
- ACK timeout
- adapter write failure
- application mailbox or queue overflow

### Acceptance criteria

- A local encode failure is not reported as a remote CRC failure.
- Tests assert the correct counter for each failure path.
- Statistics are useful for diagnosing real deployments.

---

## 27. Define short-frame and empty-delimiter behavior

### Problem

Frames shorter than five encoded bytes and repeated delimiters are silently discarded. This may be acceptable, but the behavior and statistics are not defined.

### Required change

Specify whether these events are:

- normal synchronization noise
- malformed frames that should be counted
- ignored without diagnostics

### Acceptance criteria

- Behavior is documented.
- Tests cover repeated delimiters and short frames.
- Noise does not disturb connection-phase state.

---

## 28. Consolidate protocol size constants

### Problem

Maximum payload and frame-size calculations are repeated with literal values such as `64`, `3`, `2`, and `4`. `MAX_PAYLOAD_SIZE` also means the largest payload for the current specialization rather than the protocol-wide maximum.

### Required change

Define canonical constants once for:

- protocol maximum payload
- header size
- checksum size
- maximum plain-frame size
- maximum encoded-frame size

Use distinct names for protocol-wide limits and specialization-specific buffer sizes.

### Acceptance criteria

- No duplicated magic size calculations remain in `TinyLink.h`.
- Buffer sizes derive from one canonical definition.
- Boundary tests use the same public protocol constants where appropriate.

---

## 29. Add the headers actually required by this file

### Problem

The file uses `size_t` and `std::declval` but depends on transitive inclusion for their declarations.

### Required change

Include the correct standard headers directly, including:

```cpp
#include <stddef.h>
#include <utility>
```

Use the project’s selected C or C++ standard-header style consistently.

### Acceptance criteria

- `TinyLink.h` compiles in isolation after its own direct dependencies.
- Include-order changes do not break compilation.

---

## 30. Clarify compile-time adapter validation

### Problem

The SFINAE validation checks that expressions exist but does not prove the required return types, error semantics, nonblocking behavior, or execution-context safety.

### Required change

- Document the complete adapter contract independently of compile-time detection.
- Tighten compile-time checks where reasonable.
- Do not describe the SFINAE check as proving behavioral compatibility.

### Acceptance criteria

- Every adapter method has defined inputs, outputs, blocking behavior, and failure semantics.
- Reference adapters have shared conformance tests.
- Unsupported write/read signatures fail clearly.

---

# Dependency review required

The following files must be reviewed before concluding that memory and packet processing are correct:

- `src/internal/codec/CobsCodec.h`
- `src/internal/codec/Fletcher16.h`
- `src/internal/codec/Packet.h`
- `src/internal/protocol/AckMessage.h`
- `src/internal/protocol/HandshakeMessage.h`
- `src/internal/protocol/LogMessage.h`
- `src/protocol/MessageType.h`
- `src/protocol/State.h`
- `src/protocol/Stats.h`
- every adapter implementation
- the complete test suite

## Questions for `CobsCodec.h`

- Does encoding return zero on every output-capacity failure?
- Does decoding reject zero bytes inside encoded input?
- Does decoding reject malformed code bytes and truncated runs?
- Are all reads and writes bounds-checked before access?
- Is maximum encoded size calculated correctly for every supported plain-frame size?
- Does decoding reject trailing malformed data rather than return a partial result?

## Questions for `Packet.h`

- Is the packet format exactly documented?
- Are declared payload lengths validated against decoded frame length?
- Is the checksum calculated over the intended fields?
- Does `unpack()` distinguish checksum failure from size mismatch?
- Can malformed length values cause over-read or partial payload writes?
- Is the output buffer left unchanged on failure?
- Are reserved message types validated centrally?

## Questions for `Fletcher16.h`

- Is the algorithm identical on AVR and non-AVR targets?
- Is byte signedness handled correctly?
- Are golden vectors available?
- Is the checksum's role explicitly limited to accidental-corruption detection rather than authentication?

## Questions for protocol structs

- Are exact sizes asserted?
- Are all fields initialized before transmission?
- Is text guaranteed to be terminated or safely length-bounded?
- Are enum and status field widths fixed?
- Are byte order and packing explicitly defined?

## Questions for adapters

- Can `write()` return a partial count?
- Can `write(uint8_t)` fail?
- Is `read()` guaranteed to return `-1` when no byte is available?
- Can `available()` report stale information?
- Are methods blocking?
- Is `millis()` monotonic modulo unsigned wraparound?
- Which methods are permitted in ISR context?
- Are simultaneous read/write calls safe?

---

# Required test suite

## Sequence and ACK behavior

- Matching ACK releases the pending frame.
- Stale ACK does not release the pending frame.
- Future ACK does not release the pending frame.
- Duplicate ACK is ignored or classified as unexpected.
- Unsolicited ACK is never application data.
- Envelope and ACK payload sequence mismatch is rejected.
- Second data send while ACK is pending is rejected.
- Sending a log cannot affect later acknowledgement of received data.
- Sequence wraps from 255 to 0.
- ACK timeout reports the timed-out sequence.

## Frame classification

- Every reserved internal type always uses internal dispatch.
- Reserved internal type passed to `sendData()` is rejected.
- Unknown reserved types are rejected.
- Internal frames are validated without callbacks.
- Application data with every legal application type is accepted.

## Framing and recovery

- Empty stream.
- Repeated delimiters.
- Truncated frame.
- Inter-byte timeout.
- Malformed COBS frame.
- Invalid checksum.
- Incorrect declared payload length.
- Exactly maximum payload.
- Exactly maximum encoded frame.
- Maximum plus one byte.
- Large oversized frame.
- Valid-looking suffix after overflow.
- Valid frame immediately after malformed or oversized input.
- Random garbage followed by a valid frame.

## Handshake behavior

- Initial handshake request receives a reply.
- Handshake reply completes connection.
- Unsupported handshake value does not connect.
- Both peers calling `begin()` concurrently connect.
- Handshake received while a data ACK is pending preserves the pending send.
- Repeated request while already connected follows defined behavior.
- Failed handshake write follows defined retry behavior.
- Retry limit and failure outcome work if introduced.

## Timeout behavior

- Inter-byte timeout does not destroy connection-phase state unnecessarily.
- ACK timeout is independent of partial RX accumulation.
- Unsigned time wraparound works for all timers.
- Boundary behavior at exactly `_timeout` is defined and tested.
- Handshake retry timing works across timer wraparound.

## Callback and polling behavior

- Callback receives only validated data.
- Callback can send an ACK safely in foreground context.
- Callback can send a response according to stop-and-wait rules.
- Polling `peek()` exposes stable data until consumption.
- Unconsumed polling data follows the selected backpressure policy.
- Reset preserves callbacks if that becomes the contract.
- Callback registration changes follow documented concurrency rules.

## Adapter failure behavior

- Closed adapter.
- Encoder failure.
- First delimiter write failure.
- Partial body write.
- Body write failure.
- Closing delimiter write failure.
- Port closes during transmission.
- Port closes while receiving a partial frame.

## Serialization compatibility

- Golden wire vectors for every internal message.
- Golden wire vectors for supported application primitive types.
- Cross-compiler layout verification.
- Cross-endian encode/decode verification.
- Floating-point representation tests if floating point is supported.

## Robustness testing

- Fuzz `cobs_decode()` with arbitrary byte streams.
- Fuzz `packet::unpack()` with arbitrary decoded frames.
- Mutate every byte of valid frames.
- Insert and delete every byte position in valid frames.
- Concatenate malformed and valid frames in random orders.
- Run long-duration noise and reconnect tests.
- Build with sanitizers on desktop targets.

---

# Recommended implementation order

## Phase 1: Restore protocol invariants

- [ ] Split RX, TX, and pending-ACK sequence state.
- [ ] Enforce one outstanding data frame.
- [ ] Validate ACK sequence.
- [ ] Define ACK result behavior.
- [ ] Make `sendData()` return a result.
- [ ] Decouple ACK timeout from `_rawIdx`.
- [ ] Add focused ACK and sequence tests.

## Phase 2: Correct frame routing and recovery

- [ ] Classify reserved types before state checks.
- [ ] Reject reserved application types.
- [ ] Add discard-until-delimiter overflow recovery.
- [ ] Validate handshake kind/version.
- [ ] Validate internal frames without callbacks.
- [ ] Add frame classification and overflow tests.

## Phase 3: Correct execution and lifetime model

- [ ] Remove full protocol execution from ISR context.
- [ ] Introduce an ISR-safe byte queue or polling-only contract.
- [ ] Remove implicit static-instance registration.
- [ ] Define instance lifetime and concurrency rules.
- [ ] Measure constrained-target stack use.
- [ ] Add concurrency and lifecycle tests.

## Phase 4: Define transport guarantees

- [ ] Specify adapter read/write contracts.
- [ ] Handle encoder and write failures.
- [ ] Start ACK timing only after defined send success.
- [ ] Add adapter conformance tests.

## Phase 5: Define a portable wire format

- [ ] Specify byte order.
- [ ] Add exact internal-message size assertions.
- [ ] Add trivially-copyable checks as temporary protection.
- [ ] Introduce explicit application payload encoding and decoding.
- [ ] Introduce explicit internal message encoding and decoding.
- [ ] Add golden-vector and cross-platform tests.

## Phase 6: Repair API semantics and diagnostics

- [ ] Separate connected state from send readiness.
- [ ] Preserve callbacks during protocol reset.
- [ ] Move internal helpers to `private`.
- [ ] Replace misleading error categories.
- [ ] Define polling backpressure behavior.
- [ ] Define handshake failure policy.
- [ ] Define ACK reliability guarantees.

## Phase 7: Consolidate and simplify

- [ ] Consolidate frame and payload size constants.
- [ ] Add direct standard-library includes.
- [ ] Tighten and document adapter compile-time validation.
- [ ] Split responsibilities only after behavior is covered by tests.

---

# Definition of done

TinyLink should be considered ready for a production-readiness reassessment only when all of the following are true:

- [ ] One outstanding transmitted data frame has an unambiguous sequence identity.
- [ ] Only the matching ACK can complete that frame.
- [ ] Internal messages can never be dispatched as application payloads.
- [ ] Oversized and malformed frames cannot produce valid suffix frames accidentally.
- [ ] Direct full-engine ISR execution is removed or proven safe under a deliberately constrained model.
- [ ] Instance registration cannot leave dangling pointers.
- [ ] Adapter write failures and partial writes are handled.
- [ ] Wire byte order and field representation are defined.
- [ ] Internal protocol layouts are compiler-independent.
- [ ] Application payload serialization has a defined cross-platform contract.
- [ ] Protocol validation is independent of callback registration.
- [ ] Timeout and retry semantics are documented and tested.
- [ ] Statistics identify the actual failure categories.
- [ ] Maximum stack use is measured on constrained supported targets.
- [ ] Boundary, fuzz, recovery, and long-duration tests pass.
- [ ] `TinyLink.h` and all codec, packet, protocol-message, and adapter dependencies receive a completed code review.

---

# Positive design elements to preserve

The remediation should retain the parts of the current implementation that are sound:

- fixed-size bounded buffers
- no dynamic allocation in the engine
- separate RX and local TX buffers
- unsigned elapsed-time arithmetic that tolerates timer wraparound
- small-frame protocol intent
- adapter-based transport separation
- explicit framing and checksum layers
- callback and polling consumption options, once their interaction with protocol progress is corrected

The goal is not to make TinyLink larger. It is to make the protocol invariants explicit, enforceable, and testable while preserving its small embedded footprint.
