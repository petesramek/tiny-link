# Review of `src/internal/protocol/Packed.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/internal/protocol/Packed.h`

The macro implementation is conventional and appears technically correct for the current Microsoft and GNU-compatible compiler branches. The Microsoft compiler path uses balanced packing-state push and pop operations, while the GNU-compatible path applies the packed attribute directly to individual structures.

No confirmed packing-state leak is visible in this file. The larger concern is architectural: TinyLink currently uses compiler packing as part of its wire-protocol definition. Packing controls layout and padding, but it does not define byte order, field validity, or portable serialization.

---

# Summary

## Main findings

1. Every non-MSVC compiler is assumed to support GNU packed attributes.
2. Packing does not define a portable wire format.
3. Packed multibyte members may be unaligned.
4. The MSVC macros are safe only when `BEGIN` and `END` remain correctly paired.
5. The `@file` path does not match the repository location.
6. The macros remain visible throughout the translation unit.
7. Explicit wire encoding should ultimately replace packed object serialization.

## Local assessment

The existing definitions are not a major defect by themselves. The practical local correction is to detect supported compiler families explicitly and fail clearly for unsupported compilers.

---

# P1: Tighten compiler detection

## 1. Do not treat every non-MSVC compiler as GNU-compatible

### Current behavior

The implementation has two branches:

```cpp
#if defined(_MSC_VER)
    #define TINYLINK_PACKED_BEGIN __pragma(pack(push, 1))
    #define TINYLINK_PACKED_END   __pragma(pack(pop))
    #define TINYLINK_PACKED
#else
    #define TINYLINK_PACKED_BEGIN
    #define TINYLINK_PACKED_END
    #define TINYLINK_PACKED __attribute__((packed))
#endif
```

The `else` branch assumes every compiler other than MSVC supports:

```cpp
__attribute__((packed))
```

That is likely valid for the project's current GCC, Clang, and AVR GCC targets, but it is not a general C++ guarantee.

An unsupported compiler may:

- reject the attribute
- ignore the attribute
- emit only a warning
- compile structures with an unexpected layout

The per-message `static_assert(sizeof(...))` checks reduce the risk, but they do not make the compiler selection deliberate.

### Required change

Detect supported compiler families explicitly:

```cpp
#if defined(_MSC_VER)

    #define TINYLINK_PACKED_BEGIN __pragma(pack(push, 1))
    #define TINYLINK_PACKED_END   __pragma(pack(pop))
    #define TINYLINK_PACKED

#elif defined(__GNUC__) || defined(__clang__)

    #define TINYLINK_PACKED_BEGIN
    #define TINYLINK_PACKED_END
    #define TINYLINK_PACKED __attribute__((packed))

#else

    #error "TinyLink: unsupported compiler for packed wire structures"

#endif
```

Add a dedicated branch for any additional supported compiler rather than allowing it to fall through to unverified syntax.

### Acceptance criteria

- Every supported compiler family has a deliberate implementation.
- Unsupported compilers fail clearly during compilation.
- No compiler silently uses unverified packed-attribute syntax.
- Every remaining packed wire structure has an exact size assertion.
- Compiler support documentation matches the preprocessor branches.

### Required tests

- Build with supported MSVC versions.
- Build with supported GCC versions.
- Build with supported Clang versions.
- Build with AVR GCC.
- Verify that a simulated unknown compiler path produces the intended error.

---

# P1: Do not confuse packed layout with wire portability

## 2. Packing does not define byte order or field semantics

### Current behavior

The macros control structure padding and alignment. They do not define:

- integer byte order
- floating-point representation
- enum wire values
- Boolean representation
- bit-field layout
- semantic value validation
- C++ object lifetime rules
- text termination

For example, this packed message:

```cpp
struct LogMessage {
    uint32_t ts;
    uint8_t level;
    uint16_t code;
};
```

may occupy seven bytes on both little-endian and big-endian systems, but the bytes representing `ts` and `code` will differ.

### Required change

Do not use packed object representation as TinyLink's durable canonical wire format.

Encode and decode fields explicitly:

```cpp
output[0] = static_cast<uint8_t>(value);
output[1] = static_cast<uint8_t>(value >> 8);
```

Use explicit decoders to:

- verify input length
- reconstruct multibyte values
- validate enums and discriminators
- normalize text
- construct semantic output only after complete validation

Packed structures may remain temporarily for compatibility, but they should not be extended as the primary wire-format mechanism.

### Acceptance criteria

- Every multibyte wire field has an explicit byte order.
- Enum fields have defined numeric wire values.
- Raw payloads are not cast to packed structures.
- Protocol compatibility does not depend on compiler packing.
- Golden vectors specify exact wire bytes.

---

# P1: Avoid unaligned packed-member access

## 3. Packed multibyte members can be unaligned

### Current behavior

Packing can place multibyte fields at addresses that do not satisfy their natural alignment.

The current log layout is an example:

```text
offset 0: uint32_t ts
offset 4: uint8_t level
offset 5: uint16_t code
```

The `uint16_t code` member begins at offset 5 and may be unaligned on some platforms.

Compilers normally know that members of a packed structure may be unaligned and generate suitable direct-member access. Problems arise when code:

- takes the address of a packed multibyte member
- passes it as a normally aligned pointer
- binds a reference that assumes natural alignment
- uses platform instructions requiring aligned access
- casts arbitrary raw memory to a packed object pointer

### Required change

Avoid pointers and references to packed multibyte members. Decode wire bytes into naturally aligned semantic values.

Do not use patterns such as:

```cpp
const uint16_t* code = &message.code;
```

or:

```cpp
auto* message =
    reinterpret_cast<const LogMessage*>(rawPayload);
```

Enable compiler warnings for packed-member addresses where supported.

### Acceptance criteria

- No repository code takes pointers or references to packed multibyte members.
- Raw buffers are not reinterpreted as packed message objects.
- Explicit decoders produce naturally aligned semantic values.
- Alignment-sensitive targets are included in verification.
- Relevant packed-member warnings are enabled and clean.

---

# P1: Protect packing-state pairing

## 4. `BEGIN` and `END` must remain inseparable on MSVC

### Current behavior

The MSVC implementation depends on this declaration pattern:

```cpp
TINYLINK_PACKED_BEGIN

struct Message {
    // Fields
} TINYLINK_PACKED;

TINYLINK_PACKED_END
```

The current reviewed message headers pair the macros correctly. The use of:

```cpp
pack(push, 1)
pack(pop)
```

is also preferable to setting a global packing value without preserving the previous state.

The risk is future usage discipline. For example:

```cpp
TINYLINK_PACKED_BEGIN

#if ENABLE_MESSAGE
struct Message {
};
TINYLINK_PACKED;

TINYLINK_PACKED_END
#endif
```

When `ENABLE_MESSAGE` is false, the push is not paired with a pop.

### Required change

Document that `TINYLINK_PACKED_BEGIN` and `TINYLINK_PACKED_END` must:

- appear in the same unconditional preprocessing path
- remain in the same header and declaration scope
- surround only the intended declaration
- never cross conditional-compilation boundaries
- never be separated by an early include or branch that can omit the pop

Review every current usage for balanced unconditional pairing.

Prefer removing the macros as explicit serializers replace packed structures.

### Acceptance criteria

- Every current use has a balanced unconditional pair.
- No pair crosses a conditional-compilation boundary.
- No pair crosses source files or headers.
- MSVC packing state is verified to restore after each declaration.
- New wire messages prefer explicit codecs rather than new packed declarations.

---

# P2: Correct the file documentation

## 5. Fix the `@file` path

### Current behavior

The header declares:

```cpp
@file protocol/internal/Packed.h
```

The reviewed repository location is:

```text
src/internal/protocol/Packed.h
```

The directory order in the current documentation is reversed.

### Required change

Use either the local filename:

```cpp
@file Packed.h
```

or the correct source-relative path:

```cpp
@file internal/protocol/Packed.h
```

Apply the selected convention consistently across all repository headers.

### Acceptance criteria

- Generated documentation identifies the file correctly.
- All headers use a consistent `@file` convention.

---

# P2: Keep macro scope under control

## 6. The project prefix is adequate, but the macros remain globally visible

### Current behavior

The macro names are project-prefixed:

```cpp
TINYLINK_PACKED_BEGIN
TINYLINK_PACKED_END
TINYLINK_PACKED
```

This is appropriate because macros cannot be scoped inside `namespace tinylink`.

They remain defined for the rest of the translation unit after this header is included. This is not currently a serious collision risk because the prefix is specific, but it increases global preprocessor surface area.

### Required action

Keep this header internal. Do not expose it through public API headers unless required by the current transition.

Do not attempt to `#undef` the macros at the end of this header while callers still need them. The durable correction is to remove packed declarations through explicit serialization, not to introduce more macro machinery.

### Acceptance criteria

- Macro names remain project-prefixed while they exist.
- Public consumers do not need to use these macros.
- New public types do not depend on packing macros.
- A migration plan exists for removing packed wire declarations.

---

# Compiler branch assessment

## MSVC branch

Current implementation:

```cpp
#define TINYLINK_PACKED_BEGIN __pragma(pack(push, 1))
#define TINYLINK_PACKED_END   __pragma(pack(pop))
#define TINYLINK_PACKED
```

### Positive findings

- `push` preserves the previous packing state.
- `pop` restores that previous state.
- `__pragma` can be used within a macro expansion.
- Packing applies only between the paired macros.
- The current reviewed use sites pair the operations correctly.

### Remaining risk

The branch depends on every use site preserving correct pairing across all preprocessing paths.

---

## GCC and Clang branch

Current implementation:

```cpp
#define TINYLINK_PACKED_BEGIN
#define TINYLINK_PACKED_END
#define TINYLINK_PACKED __attribute__((packed))
```

### Positive findings

- The attribute applies directly to the annotated structure.
- There is no translation-unit packing state to restore.
- An omitted `TINYLINK_PACKED_END` has no effect in this branch.
- The approach is conventional for GCC, Clang, and AVR GCC.

### Remaining risk

The branch is reached for every non-MSVC compiler, including compilers that may not support GNU attribute syntax.

---

# Required test suite

## Compiler-layout probe

Define a representative packed structure:

```cpp
TINYLINK_PACKED_BEGIN
struct PackedProbe {
    uint8_t first;
    uint32_t second;
    uint16_t third;
} TINYLINK_PACKED;
TINYLINK_PACKED_END
```

Verify:

```cpp
static_assert(sizeof(PackedProbe) == 7,
              "PackedProbe size mismatch");
static_assert(offsetof(PackedProbe, first) == 0,
              "PackedProbe.first offset mismatch");
static_assert(offsetof(PackedProbe, second) == 1,
              "PackedProbe.second offset mismatch");
static_assert(offsetof(PackedProbe, third) == 5,
              "PackedProbe.third offset mismatch");
```

Run the probe on every supported compiler and target.

## Packing-restoration probe

Especially on MSVC, declare an ordinary structure after the packed declaration:

```cpp
struct NormalProbe {
    uint8_t first;
    uint32_t second;
};
```

Compare it with a control declaration that was never inside packing macros. Verify that the post-packed declaration uses the compiler's normal alignment rather than pack value 1.

Do not assert one universal size unless each supported ABI guarantees it. The purpose is to verify restoration relative to the normal compiler layout.

## Existing message-layout tests

While packed serialization remains:

- [ ] `sizeof(TinyAck)` matches its expected layout.
- [ ] `offsetof(TinyAck, seq)` is correct.
- [ ] `offsetof(TinyAck, result)` is correct.
- [ ] `sizeof(HandshakeMessage)` matches its expected layout.
- [ ] `sizeof(LogMessage)` is 32.
- [ ] Every `LogMessage` field offset is asserted.
- [ ] `sizeof(TinyStatus)` is asserted independently.
- [ ] Every assertion passes on every supported compiler.

## Alignment tests

- [ ] Decode log messages on an alignment-sensitive target.
- [ ] Confirm no pointer to a packed multibyte field is passed as an aligned pointer.
- [ ] Enable warnings for taking addresses of packed members where available.
- [ ] Run UndefinedBehaviorSanitizer on desktop builds.
- [ ] Remove raw-buffer-to-packed-structure casts.

## Unsupported-compiler behavior

- [ ] Verify an unrecognized compiler branch produces a clear compile-time error.
- [ ] Verify the error identifies packed wire structures as the unsupported capability.

## Packing-pair audit

- [ ] Search for every `TINYLINK_PACKED_BEGIN`.
- [ ] Confirm a corresponding `TINYLINK_PACKED_END` exists in the same unconditional path.
- [ ] Confirm no pair crosses `#if`, `#else`, or `#endif` asymmetrically.
- [ ] Confirm no unrelated declaration is accidentally included inside the packed region.

---

# Implementation checklist

## Compiler support

- [ ] Replace the broad non-MSVC fallback with explicit compiler-family detection.
- [ ] Add a clear unsupported-compiler error.
- [ ] Document supported compiler families.
- [ ] Add packing probes to continuous integration.

## Layout verification

- [ ] Add exact size assertions for every packed wire structure.
- [ ] Add exact field-offset assertions for every packed wire structure.
- [ ] Add an MSVC packing-restoration probe.
- [ ] Verify every packed macro pair is unconditional and balanced.

## Alignment safety

- [ ] Remove raw payload casts to packed structures.
- [ ] Do not take pointers or references to packed multibyte members.
- [ ] Enable available packed-member address warnings.
- [ ] Test alignment-sensitive targets.
- [ ] Run sanitizer builds where available.

## Protocol migration

- [ ] Define byte order independently of packing.
- [ ] Introduce explicit ACK encoding and decoding.
- [ ] Introduce explicit handshake encoding and decoding.
- [ ] Introduce explicit log encoding and decoding.
- [ ] Keep semantic public types naturally aligned.
- [ ] Remove packed declarations when no wire codec depends on them.
- [ ] Remove this header when the migration is complete.

## Documentation

- [ ] Correct the `@file` path.
- [ ] Document mandatory `BEGIN` and `END` pairing.
- [ ] Document that packing does not define byte order.
- [ ] Keep this header internal.

---

# Definition of done

The `Packed.h` remediation is complete when:

- [ ] Supported compiler families are detected explicitly.
- [ ] Unsupported compilers fail clearly.
- [ ] Every remaining packed structure has exact size and field-offset assertions.
- [ ] MSVC packing state is proven to restore after each declaration.
- [ ] No macro pair crosses conditional-compilation boundaries.
- [ ] No code casts raw payloads directly to packed structures.
- [ ] No code takes potentially misaligned pointers to packed multibyte members.
- [ ] Byte order is defined independently from structure layout.
- [ ] New protocol work uses explicit encoding and decoding.
- [ ] Existing packed wire structures have a documented migration path.
- [ ] The inaccurate `@file` path is corrected.
- [ ] This header is removed once no packed wire structure requires it.

---

# Verdict

`Packed.h` itself is not a major defect. Its macro definitions are conventional, and the MSVC branch correctly preserves and restores the previous packing state.

The actionable local correction is to replace the unrestricted non-MSVC fallback with explicit supported-compiler detection.

The larger correction belongs at the protocol level. Packing can control field placement, but it cannot create a portable, validated wire format. As explicit codecs are introduced for ACK, handshake, and log messages, this header should become unnecessary.

## Next review target

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/protocol/Status.h`
