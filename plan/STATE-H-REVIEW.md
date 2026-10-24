# Review of `src/protocol/State.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/State.h`

`State.h` is mechanically sound as an enum definition and contains no local memory-safety defect. The important problem is architectural: `TinyState` combines connection lifecycle, receive-parser progress, and outbound ACK waiting into one mutually exclusive state value.

That design cannot represent normal simultaneous conditions such as a connected link receiving a frame while awaiting acknowledgement of a previously transmitted frame. It also explains the manual state preservation and restoration logic in `TinyLink.h`.

---

# Summary

## Positive findings

1. `TinyState` has an explicit `uint8_t` underlying type.
2. The state names are understandable in isolation.
3. The documentation attempts to describe the engine transitions.
4. The enum is compact and suitable for embedded targets.
5. No direct memory-safety problem exists in this file.

## Main problems

1. Connection lifecycle, RX parser progress, and TX ACK waiting are conflated.
2. `WAIT_FOR_SYNC` combines connected status with parser-idle status.
3. `IN_FRAME` cannot coexist with `AWAITING_ACK`.
4. `FRAME_COMPLETE` is a transient implementation operation rather than durable protocol state.
5. Construction and reset have no explicit inactive state.
6. Failure, incompatibility, and disconnection cannot be represented.
7. Documentation does not describe all actual transitions in `TinyLink.h`.
8. Enum values are implicit even though `state()` exposes them publicly.
9. Public consumers can observe parser implementation details.

---

# P0: Separate independent state machines

## 1. One enum currently represents three independent dimensions

### Current model

```cpp
enum class TinyState : uint8_t {
    CONNECTING,
    WAIT_FOR_SYNC,
    IN_FRAME,
    FRAME_COMPLETE,
    AWAITING_ACK
};
```

The values belong to three distinct concerns.

### Connection lifecycle

```text
inactive
connecting
connected
incompatible
failed
```

### Receive parser

```text
waiting for a frame
receiving a frame
discarding an oversized frame
```

### Transmit acknowledgement

```text
ready
awaiting acknowledgement
```

These dimensions can be active simultaneously.

For example, TinyLink can legitimately be:

```text
Connection: connected
Receive: receiving a frame
Transmit: awaiting ACK
```

The current enum cannot represent that combination.

### Current workaround in `TinyLink.h`

When receiving bytes while awaiting an ACK, the implementation does not enter `IN_FRAME`:

```cpp
if (_state == TinyState::WAIT_FOR_SYNC) {
    _state = TinyState::IN_FRAME;
}
```

At a frame boundary, it stores the prior combined state:

```cpp
TinyState priorState = _state;
_state = TinyState::FRAME_COMPLETE;
```

Handlers then restore either `AWAITING_ACK` or `WAIT_FOR_SYNC` through logic such as:

```cpp
restoreState(priorState)
```

This preservation logic exists because one variable is being used for several independent responsibilities.

### Required change

Split the state model into independent enums.

Suggested model:

```cpp
enum class ConnectionState : uint8_t {
    Inactive,
    Connecting,
    Connected,
    Incompatible,
    Failed
};

enum class ReceiveState : uint8_t {
    WaitingForFrame,
    ReceivingFrame,
    DiscardingFrame
};

enum class TransmitState : uint8_t {
    Ready,
    AwaitingAck
};
```

The exact public names can follow the repository's chosen naming style. The important correction is separation of responsibilities.

### Acceptance criteria

- Receiving a frame can coexist with awaiting an ACK.
- Connection status is independent of parser progress.
- Parser timeout changes only receive state and diagnostics.
- ACK receipt or timeout changes only transmit state and diagnostics.
- State transitions no longer require `restoreState(priorState)`.
- Each state enum has one clear responsibility.
- Invalid combinations are minimized and tested.

---

# P0: Add discard-until-delimiter state

## 2. RX overflow needs an explicit discard state

### Current behavior

The earlier `TinyLink.h` review identified that RX overflow currently resets the index:

```cpp
_rawIdx = 0;
```

The engine then accumulates the remaining bytes of the same oversized frame as though they begin a new frame.

The correct behavior is to ignore the remainder of the oversized frame until the next `0x00` delimiter.

### Required change

Represent that behavior explicitly:

```cpp
enum class ReceiveState : uint8_t {
    WaitingForFrame,
    ReceivingFrame,
    DiscardingFrame
};
```

Required transitions:

```text
ReceivingFrame + buffer overflow
    -> DiscardingFrame

DiscardingFrame + nonzero byte
    -> DiscardingFrame

DiscardingFrame + 0x00 delimiter
    -> WaitingForFrame
```

### Acceptance criteria

- No suffix of an oversized frame is decoded.
- Overflow is counted once per discarded frame.
- The next valid frame is accepted normally.
- Discarding a frame does not alter connection state.
- Discarding a frame does not alter pending-ACK state.

### Required tests

- Exactly maximum encoded frame size.
- Maximum plus one byte.
- Much larger oversized frame.
- Oversized frame with a suffix that is itself valid COBS.
- Valid frame immediately after the oversized frame.
- Overflow while awaiting a data ACK.

---

# P1: Rename `WAIT_FOR_SYNC`

## 3. The name does not match actual behavior

### Current documentation

```cpp
WAIT_FOR_SYNC
```

is described as:

```text
Connected and idle; waiting for next 0x00 frame delimiter
```

### Actual behavior

The first nonzero byte begins frame accumulation:

```cpp
if (_state == TinyState::WAIT_FOR_SYNC) {
    _state = TinyState::IN_FRAME;
}
```

The engine is not waiting for a delimiter before beginning the frame. Repeated delimiters are synchronization markers, while a nonzero byte starts encoded frame data.

The name also combines:

- connected lifecycle status
- parser-idle status

### Required change

After separating state dimensions, use independent semantic names.

For receive state:

```cpp
WaitingForFrame
```

or:

```cpp
Idle
```

For connection state:

```cpp
Connected
```

### Acceptance criteria

- Parser state names describe actual parser behavior.
- Connected status is not inferred from receive-parser terminology.
- Leading, repeated, and trailing delimiter behavior is documented accurately.

---

# P1: Remove `FRAME_COMPLETE` from persistent state

## 4. Frame dispatch is an operation, not durable protocol state

### Current behavior

`FRAME_COMPLETE` exists briefly while a completed frame is decoded and dispatched.

Callbacks can observe it:

```cpp
_onDataReceived(_data);
```

The engine then applies special logic:

```cpp
if (_state == TinyState::FRAME_COMPLETE) {
    _state = nextState;
}
```

This allows callback activity to influence state restoration indirectly.

### Problem

`FRAME_COMPLETE` describes an implementation step rather than a stable protocol condition. Exposing it through `state()` couples public consumers to parser internals.

It also complicates callback reentrancy because the engine must infer whether the callback changed state deliberately.

### Required change

Remove `FRAME_COMPLETE` from persistent protocol state.

Use ordinary local control flow:

```cpp
dispatchFrame(...);
```

If callback reentrancy must be tracked, use a dedicated internal flag:

```cpp
bool _isDispatching;
```

That flag should remain private and should not be presented as connection or protocol state.

### Acceptance criteria

- Dispatch progress is not publicly exposed as connection state.
- Callbacks do not rely on observing `FRAME_COMPLETE`.
- Callback-initiated sends follow explicit rules.
- State after callback execution is deterministic.
- Reentrancy, if supported, is modeled directly.

---

# P1: Add explicit inactive lifecycle state

## 5. Construction and reset currently reuse a connected-idle parser state

### Current behavior

Construction initializes:

```cpp
_state = TinyState::WAIT_FOR_SYNC;
_handshakeEnabled = false;
```

Before `begin()` is called, `connected()` returns true whenever the adapter is open:

```cpp
if (!_handshakeEnabled) {
    return true;
}
```

`WAIT_FOR_SYNC` therefore represents several different situations:

- construction before handshake mode is enabled
- handshake-free transport mode
- connected handshake mode
- protocol state after reset

### Required change

Add an explicit lifecycle state:

```cpp
ConnectionState::Inactive
```

Suggested lifecycle:

```text
construction -> Inactive
begin() -> Connecting
valid compatible handshake -> Connected
reset() -> Inactive
```

If handshake-free operation remains supported, expose it as deliberate configuration or startup behavior rather than inferring it from whether `begin()` has been called.

For example:

```text
startWithoutHandshake() -> Connected
```

or define transport-open readiness separately from negotiated connection status.

### Acceptance criteria

- Construction state is explicit.
- Reset state is explicit.
- `connected()` has one stable meaning.
- Handshake-free operation is intentional and documented.
- Connection lifecycle does not depend on parser state.

---

# P1: Add failure and incompatibility outcomes

## 6. The current lifecycle can represent only connecting or connected

### Current behavior

The combined enum has no state for:

- adapter closed
- connection failed
- handshake retry exhausted
- incompatible protocol version
- stopped operation
- disconnected after a previous connection

The current handshake retries indefinitely, which avoids representing failure but prevents callers from distinguishing temporary absence from permanent incompatibility.

### Required change

Define lifecycle states appropriate to the final handshake policy:

```cpp
enum class ConnectionState : uint8_t {
    Inactive,
    Connecting,
    Connected,
    Incompatible,
    Failed
};
```

A separate `Disconnected` state may be useful if loss of a previously established connection must be distinguished from inactivity or failed startup.

### Acceptance criteria

- Unsupported protocol version has a specific outcome.
- Retry exhaustion has a defined outcome if retries are bounded.
- Adapter closure has documented state behavior.
- Applications can distinguish connecting from failed.
- Connection callbacks use semantic lifecycle states.
- Recovery from failed or disconnected state is documented.

---

# P1: Separate connection from send readiness

## 7. `AWAITING_ACK` is a transmit condition, not a connection state

### Current behavior

`connected()` returns true for `AWAITING_ACK`, which is reasonable because the link remains connected.

However, `sendData()` also uses `connected()` to determine whether sending is allowed. That permits another data send while the previous frame is awaiting an ACK.

### Required change

Expose independent semantic queries:

```cpp
bool connected() const;
bool canSend() const;
bool awaitingAck() const;
```

Track transmit readiness independently:

```cpp
enum class TransmitState : uint8_t {
    Ready,
    AwaitingAck
};
```

### Acceptance criteria

- A connected link can report that it is temporarily unable to send.
- Stop-and-wait mode rejects a second data send.
- Receiving frames does not change transmit readiness.
- ACK receipt changes only transmit state.
- ACK timeout changes only transmit state and diagnostics unless the protocol explicitly disconnects.
- Connection remains connected after an ordinary ACK timeout unless the selected policy says otherwise.

---

# P1: Align documentation with actual transitions

## 8. Several documented transitions are incomplete

### Handshake transition

The documentation states:

```text
CONNECTING + HS(v=0) or HS(v=1)
    -> WAIT_FOR_SYNC
```

The current implementation accepts every other handshake byte as establishing the connection as well. The documentation reflects intended behavior, but the implementation is broader.

### Frame-boundary transition

The documentation states:

```text
IN_FRAME + 0x00
    -> FRAME_COMPLETE
```

A frame delimiter can also be processed while the combined state remains `AWAITING_ACK`, because incoming data can be accumulated without changing that state.

### ACK transition

The documentation states:

```text
AWAITING_ACK -> WAIT_FOR_SYNC
```

on ACK receipt or timeout. The current implementation accepts any structurally valid ACK without validating its sequence.

### Required change

Correct the implementation first, then document independent transition sets for:

- connection lifecycle
- RX parser
- TX acknowledgement

Do not retain one combined transition narrative that hides simultaneous state.

### Acceptance criteria

- Documentation matches executable behavior.
- Every accepted handshake kind is documented.
- ACK transition requires a matching pending sequence.
- RX transitions are documented independently of TX state.
- Transition diagrams cover failure and discard states.

---

# P1: Remove parser internals from the public state API

## 9. `TinyLink::state()` exposes implementation details

### Current behavior

The public API returns the combined state:

```cpp
TinyState state() const {
    return _state;
}
```

Consumers can therefore depend on parser-specific values such as:

```cpp
IN_FRAME
FRAME_COMPLETE
```

That makes parser refactoring a public API change.

### Required change

Expose semantic public state instead:

```cpp
ConnectionState connectionState() const;
bool connected() const;
bool canSend() const;
bool awaitingAck() const;
```

Keep receive-parser state private unless there is a genuine diagnostics requirement.

If parser diagnostics are needed, expose them through a separate diagnostic API with explicitly weaker compatibility guarantees.

### Acceptance criteria

- Public API exposes connection and send readiness semantically.
- RX parser implementation stages remain private.
- Removing `FRAME_COMPLETE` does not break ordinary consumers.
- Public state values have documented compatibility guarantees.
- Public callbacks receive semantic lifecycle events rather than parser stages.

---

# P2: Decide whether public state values need stable numbers

## 10. Current enumerator values are implicit

### Current behavior

```cpp
enum class TinyState : uint8_t {
    CONNECTING,
    WAIT_FOR_SYNC,
    IN_FRAME,
    FRAME_COMPLETE,
    AWAITING_ACK
};
```

The numeric values are implicitly assigned as `0` through `4`.

If the enum were internal only, that would be harmless. Because `state()` exposes it publicly, consumers, tests, logs, and bindings can begin depending on those values.

### Required decision

If a replacement semantic connection state remains public and numeric stability matters, assign explicit values.

Do not stabilize the current combined enum merely to preserve parser implementation details. Replace the API first.

### Acceptance criteria

- Only intentionally public states receive stable numeric assignments.
- Internal parser states remain free to evolve.
- Tests distinguish semantic API compatibility from implementation details.

---

# P2: Naming style

The members use uppercase names:

```cpp
CONNECTING
WAIT_FOR_SYNC
IN_FRAME
FRAME_COMPLETE
AWAITING_ACK
```

This matches the current style of `LogLevel`, though PascalCase is common for modern C++ enum members.

This is not a correctness issue. Do not combine a broad naming change with the protocol-state remediation unless the repository adopts one deliberate public enum convention.

---

# P2: Correct file metadata

## 11. Use the source-root-relative `@file` convention

### Current behavior

```cpp
@file State.h
```

The reviewed source path is:

```text
src/protocol/State.h
```

Other public protocol files use source-root-relative documentation, for example:

```cpp
@file protocol/LogLevel.h
```

### Required change

Use:

```cpp
@file protocol/State.h
```

### Acceptance criteria

- Public protocol headers use one consistent `@file` convention.
- Generated documentation identifies the repository location accurately.

---

# Proposed minimal state model

The following is a minimal separation that can represent the current engine responsibilities without introducing unnecessary complexity.

## Connection state

```cpp
enum class ConnectionState : uint8_t {
    Inactive = 0,
    Connecting = 1,
    Connected = 2,
    Incompatible = 3,
    Failed = 4
};
```

## Receive state

```cpp
enum class ReceiveState : uint8_t {
    WaitingForFrame = 0,
    ReceivingFrame = 1,
    DiscardingFrame = 2
};
```

## Transmit state

```cpp
enum class TransmitState : uint8_t {
    Ready = 0,
    AwaitingAck = 1
};
```

This permits ordinary combinations such as:

```text
Connection: Connected
Receive: ReceivingFrame
Transmit: AwaitingAck
```

No saved combined state is needed while dispatching log, ACK, or handshake frames.

---

# Required test suite

## Connection lifecycle

- [ ] Construction begins inactive.
- [ ] `begin()` moves to connecting.
- [ ] Valid compatible handshake moves to connected.
- [ ] Invalid handshake kind does not connect.
- [ ] Unsupported version moves to incompatible or follows the chosen policy.
- [ ] Retry exhaustion moves to failed if retries are bounded.
- [ ] Reset moves to inactive.
- [ ] Adapter closure follows documented behavior.
- [ ] Reconnection follows documented behavior.

## Receive parser

- [ ] Leading delimiters keep the parser waiting.
- [ ] First nonzero byte enters receiving state.
- [ ] Delimiter completes the current frame.
- [ ] Successful dispatch returns to waiting state.
- [ ] Inter-byte timeout returns to waiting state.
- [ ] Overflow enters discarding state.
- [ ] Nonzero bytes remain discarded.
- [ ] Delimiter exits discarding state.
- [ ] The next valid frame succeeds.

## Transmit ACK state

- [ ] Successful data send enters awaiting-ACK state.
- [ ] Second send is rejected in stop-and-wait mode.
- [ ] Matching ACK returns to ready.
- [ ] Stale ACK does not change transmit state.
- [ ] Duplicate ACK does not change transmit state.
- [ ] Future ACK does not change transmit state.
- [ ] ACK timeout returns to ready and reports timeout.
- [ ] Handshake reception does not clear pending ACK state.
- [ ] Log reception does not clear pending ACK state.

## Combined-state behavior

- [ ] Receive data while awaiting ACK.
- [ ] Receive log while awaiting ACK.
- [ ] Receive handshake while awaiting ACK.
- [ ] RX timeout while awaiting ACK preserves transmit state.
- [ ] ACK timeout while receiving a partial frame preserves RX state.
- [ ] RX overflow while awaiting ACK preserves transmit state.
- [ ] Invalid handshake while awaiting ACK preserves transmit state.
- [ ] Connection failure has defined effects on receive and transmit state.

## Public API

- [ ] `connected()` depends only on connection state.
- [ ] `canSend()` considers connection and transmit state.
- [ ] `awaitingAck()` reflects only transmit state.
- [ ] Public consumers do not depend on RX parser internals.
- [ ] Callback code never observes a transient dispatch state.
- [ ] Semantic connection events have stable documented values if exposed numerically.

---

# Implementation checklist

## State separation

- [ ] Introduce independent connection state.
- [ ] Introduce independent receive-parser state.
- [ ] Introduce independent transmit state.
- [ ] Remove the combined `_state` field.
- [ ] Remove `restoreState(priorState)`.

## Receive parser

- [ ] Add discard-until-delimiter state.
- [ ] Replace `WAIT_FOR_SYNC` with accurate receive terminology.
- [ ] Keep parser state private.
- [ ] Ensure RX timeout changes only parser state.
- [ ] Ensure overflow changes only parser state and statistics.

## Frame dispatch

- [ ] Remove `FRAME_COMPLETE` from persistent state.
- [ ] Track callback dispatch separately only if required.
- [ ] Define callback reentrancy rules.
- [ ] Ensure callback sends do not rely on implicit state restoration.

## Connection lifecycle

- [ ] Add inactive state.
- [ ] Add incompatible state if protocol versioning is introduced.
- [ ] Add failed state if retry or transport failure is surfaced.
- [ ] Define adapter-closure behavior.
- [ ] Define reset and reconnection behavior.
- [ ] Make handshake-free mode explicit if retained.

## Transmit readiness

- [ ] Move `AWAITING_ACK` to transmit state.
- [ ] Separate `connected()` from `canSend()`.
- [ ] Add `awaitingAck()` query if useful.
- [ ] Enforce one pending data frame in stop-and-wait mode.
- [ ] Keep ACK timeout independent of RX state.

## Public API

- [ ] Replace public combined `state()` with semantic queries.
- [ ] Expose only stable connection lifecycle state.
- [ ] Keep parser implementation details internal.
- [ ] Assign explicit numeric values only to intentionally stable public enums.

## Documentation

- [ ] Rewrite transitions as independent state-machine sections.
- [ ] Document simultaneous state combinations.
- [ ] Correct handshake and ACK transition prerequisites.
- [ ] Correct `@file` to `protocol/State.h`.

## Verification

- [ ] Add connection lifecycle tests.
- [ ] Add RX parser tests.
- [ ] Add transmit ACK tests.
- [ ] Add combined-state tests.
- [ ] Add public API behavior tests.

---

# Definition of done

The state-model remediation is complete when:

- [ ] Connection lifecycle is independent from RX parsing.
- [ ] TX ACK waiting is independent from connection and RX parsing.
- [ ] Receiving a frame can coexist with awaiting an ACK.
- [ ] RX overflow enters discard-until-delimiter state.
- [ ] Frame dispatch is an operation rather than publicly visible state.
- [ ] Construction, reset, failure, and incompatibility have explicit semantics.
- [ ] `connected()` and `canSend()` answer different questions.
- [ ] No handler needs to save and restore a combined state.
- [ ] Public consumers do not observe parser implementation details.
- [ ] Connection, receive, transmit, combined-state, and public API tests pass.

---

# Verdict

`State.h` is valid C++ and contains no direct memory-safety flaw. The problem is that `TinyState` combines independent behavioral dimensions into one mutually exclusive enum.

That design forces `TinyLink.h` to preserve and restore state manually and prevents the type system from representing ordinary conditions such as:

```text
connected + receiving + awaiting ACK
```

The correct fix is not to add more members to `TinyState`. It is to split the state model into connection, receive, and transmit concerns.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Stats.h`
