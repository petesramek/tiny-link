# Review of `src/Version.h`

## Scope

This document records the findings from the review of:

- **Project:** `petesramek/tiny-link`
- **Relative path:** `src/Version.h`

`Version.h` is simple and correct for the current library release `0.4.0`. The major, minor, patch, combined numeric version, and string representation are mutually consistent.

The main concern is conceptual: this header defines the **library release version**, while the TinyLink handshake needs an independent **wire-protocol version**. The library version must not automatically determine peer compatibility.

---

# Summary

## Positive findings

1. Major, minor, and patch components are explicit.
2. `TINYLINK_VERSION` correctly evaluates to `400` for version `0.4.0`.
3. `TINYLINK_VERSION_STR` matches the numeric components.
4. The combined integer supports simple compile-time feature checks.
5. The header has no runtime, allocation, or platform-specific requirements.
6. No local memory-safety or correctness defect is present for the current values.

## Improvements needed

1. Separate library release version from wire-protocol version.
2. Derive the combined numeric version from the components.
3. Derive the version string from the components.
4. Add range checks for minor and patch values.
5. Document compile-time comparison semantics.
6. Define prerelease and build-metadata policy.
7. Align the include guard with the repository naming convention.

---

# P0: Separate library and protocol versioning

## 1. Do not use `TINYLINK_VERSION` as the handshake protocol version

### Current purpose

The header describes itself as:

> Version tracking for the TinyLink library.

The following values therefore represent a software release:

```cpp
TINYLINK_VERSION_MAJOR
TINYLINK_VERSION_MINOR
TINYLINK_VERSION_PATCH
TINYLINK_VERSION
TINYLINK_VERSION_STR
```

### Protocol concern

The reviewed handshake currently has a field named `version`, although `TinyLink.h` uses that byte as a request/response discriminator:

```text
0 = handshake request
1 = handshake response
```

Once the handshake model is corrected, TinyLink will need an actual wire-protocol compatibility value.

That value must not automatically be `TINYLINK_VERSION`.

A library can release:

- implementation fixes
- documentation changes
- API additions
- new adapters
- platform support

without changing its wire format.

A wire-format change can also require a protocol version even when the broader library release process follows a different cadence.

### Required change

Define separate concepts.

## Library release version

```cpp
#define TINYLINK_VERSION_MAJOR 0
#define TINYLINK_VERSION_MINOR 4
#define TINYLINK_VERSION_PATCH 0
```

## Wire-protocol version

For example:

```cpp
#define TINYLINK_PROTOCOL_VERSION 1
```

If compatibility ranges are needed:

```cpp
#define TINYLINK_PROTOCOL_VERSION_MIN 1
#define TINYLINK_PROTOCOL_VERSION_MAX 1
```

The handshake should exchange the protocol version or compatibility range rather than the combined library release integer.

### Acceptance criteria

- Library release version and wire-protocol version are separate definitions.
- A library patch release does not automatically change protocol compatibility.
- Every wire-breaking change requires an explicit protocol-version decision.
- The handshake exchanges protocol compatibility data, not the library release version.
- Multiple library releases can share one protocol version.
- Compatible and incompatible protocol versions are tested.

---

# P1: Derive the combined numeric version

## 2. `TINYLINK_VERSION` duplicates the component values

### Current behavior

```cpp
#define TINYLINK_VERSION 400

#define TINYLINK_VERSION_MAJOR 0
#define TINYLINK_VERSION_MINOR 4
#define TINYLINK_VERSION_PATCH 0
```

The values are currently consistent:

```text
0 * 10000 + 4 * 100 + 0 = 400
```

Future maintainers must update both the individual components and the combined integer manually.

A release could accidentally contain inconsistent definitions such as:

```cpp
#define TINYLINK_VERSION 401
#define TINYLINK_VERSION_PATCH 2
```

### Required change

Derive the combined value from the components:

```cpp
#define TINYLINK_VERSION_ENCODE(major, minor, patch)           \
    (((major) * 10000) + ((minor) * 100) + (patch))

#define TINYLINK_VERSION                                      \
    TINYLINK_VERSION_ENCODE(                                  \
        TINYLINK_VERSION_MAJOR,                               \
        TINYLINK_VERSION_MINOR,                               \
        TINYLINK_VERSION_PATCH)
```

### Acceptance criteria

- Major, minor, and patch are the single numeric source of truth.
- Changing a component automatically changes `TINYLINK_VERSION`.
- Compile-time version comparisons continue to work.
- `0.4.0` encodes as `400`.
- The encoding helper is covered by tests.

---

# P1: Derive the string representation

## 3. `TINYLINK_VERSION_STR` duplicates the component values

### Current behavior

```cpp
#define TINYLINK_VERSION_STR "0.4.0"
```

This string must be manually synchronized with the numeric components.

### Required change

Use two-stage stringification:

```cpp
#define TINYLINK_STRINGIFY_IMPL(value) #value
#define TINYLINK_STRINGIFY(value) TINYLINK_STRINGIFY_IMPL(value)

#define TINYLINK_VERSION_STR                                  \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MAJOR) "."            \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MINOR) "."            \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_PATCH)
```

Adjacent string literals are concatenated at compile time, producing:

```text
"0.4.0"
```

### Acceptance criteria

- The version string is derived from the numeric components.
- Component changes automatically update the string.
- The result remains a compile-time string literal.
- No runtime formatting or allocation is introduced.

---

# P1: Constrain component ranges

## 4. The numeric encoding assumes minor and patch are below 100

### Current formula

```text
major * 10000 + minor * 100 + patch
```

This is unambiguous only when:

```text
0 <= minor < 100
0 <= patch < 100
```

For example:

```text
0.4.100 -> 500
0.5.0   -> 500
```

Both versions produce the same combined value.

### Required change

Document and verify the component ranges:

```cpp
static_assert(
    TINYLINK_VERSION_MINOR < 100,
    "TinyLink minor version must be less than 100");

static_assert(
    TINYLINK_VERSION_PATCH < 100,
    "TinyLink patch version must be less than 100");
```

The current definitions are nonnegative integer literals, so lower-bound assertions are optional. They may still be added to make the full contract explicit.

If more than 99 minor or patch releases must be representable, adopt a different numeric encoding.

### Acceptance criteria

- Minor and patch ranges are documented.
- Invalid component values fail compilation or contract tests.
- Different supported semantic versions cannot encode to the same integer.
- Comparison behavior remains predictable.

---

# P1: Document comparison semantics

## 5. Numeric comparison supports only normalized release components

### Current use case

The combined integer supports checks such as:

```cpp
#if TINYLINK_VERSION >= 400
    // Feature available since 0.4.0.
#endif
```

This works correctly while:

- components remain within their documented ranges
- versions are normalized
- prerelease ordering is excluded

### Required documentation

State explicitly that `TINYLINK_VERSION` represents only:

```text
major.minor.patch
```

It does not encode:

- prerelease ordering
- build metadata
- Git commit identity
- wire-protocol compatibility

Where practical, feature checks should use the encoding helper rather than unexplained numeric literals:

```cpp
#if TINYLINK_VERSION >= TINYLINK_VERSION_ENCODE(0, 4, 0)
```

### Acceptance criteria

- Compile-time comparison semantics are documented.
- Examples avoid unexplained magic version integers where practical.
- Protocol compatibility never relies solely on library-version comparison.
- Prerelease behavior follows an explicit policy.

---

# P2: Define prerelease and build metadata policy

## 6. The current representation covers stable three-part versions only

### Current capability

The header supports:

```text
0.4.0
```

It does not represent Semantic Versioning strings such as:

```text
0.4.0-alpha.1
0.4.0-rc.2
0.4.0+build.17
```

This is not a defect if the project does not need those forms.

### Required decision

Choose one policy.

## Stable release components only

Keep the existing three-part numeric model and document that prerelease and build metadata are not represented.

## Optional separate metadata

For example:

```cpp
#define TINYLINK_VERSION_PRERELEASE "rc.1"
#define TINYLINK_VERSION_BUILD ""
```

Do not claim that the combined integer models full Semantic Versioning precedence unless a complete comparison policy is introduced.

### Recommendation

Keep the numeric version simple. If prerelease labels are needed, expose them separately in the display string or build metadata.

### Acceptance criteria

- The release policy states whether prerelease identifiers are supported.
- Numeric comparison is not used to compare prerelease precedence incorrectly.
- Build metadata does not affect protocol compatibility.

---

# P2: Align the include guard

## 7. The guard differs from the repository's `TINYLINK_` convention

### Current guard

```cpp
#ifndef TINY_LINK_VERSION_H
#define TINY_LINK_VERSION_H
```

Most reviewed headers use the prefix:

```text
TINYLINK_
```

without the underscore between `TINY` and `LINK`.

### Required change

Use:

```cpp
#ifndef TINYLINK_VERSION_H
#define TINYLINK_VERSION_H
```

### Compatibility note

Include-guard names are not normally part of the public C++ API. Search the repository before changing the guard to ensure no source file or build script tests the old macro directly.

### Acceptance criteria

- Include guards follow one repository convention.
- No code depends on the old guard macro.
- Header inclusion remains correct after the rename.

---

# P2: Keep source-root-relative file metadata

## 8. The current `@file` path is acceptable

### Current documentation

```cpp
@file Version.h
```

The reviewed location is:

```text
src/Version.h
```

Because the file is directly under `src`, `Version.h` is already source-root-relative.

No path correction is needed if the repository standard is relative to `src`.

---

# P2: Retain macros where preprocessing is required

## 9. Macros are appropriate for preprocessor feature checks

The file explicitly supports compile-time feature detection:

```cpp
#if TINYLINK_VERSION >= 400
```

Macros are appropriate for this use.

Typed constants could optionally be added for ordinary C++ code:

```cpp
namespace tinylink {
namespace version {

static constexpr uint8_t Major = 0;
static constexpr uint8_t Minor = 4;
static constexpr uint8_t Patch = 0;

} // namespace version
} // namespace tinylink
```

### Recommendation

Retain macros for preprocessing. Add typed constants only if runtime C++ access is genuinely useful, and derive them from one source rather than maintaining duplicate values.

### Acceptance criteria

- Preprocessor feature checks remain supported.
- No second manually maintained version source is introduced.
- Typed access, if added, derives from the macro components or a generated source.

---

# Positive findings

The following characteristics should be retained.

## Simple compile-time feature detection

The combined integer enables zero-cost checks:

```cpp
#if TINYLINK_VERSION >= 400
```

## Explicit components

Consumers can inspect major, minor, and patch separately.

## Compile-time string literal

The version string requires no runtime formatting or allocation.

## Current consistency

All current definitions correctly represent `0.4.0`.

## Platform independence

The header has no architecture-specific dependencies.

---

# Suggested corrected form

A minimal corrected header could follow this structure:

```cpp
/**
 * @file Version.h
 * @brief Library release version for TinyLink.
 */

#ifndef TINYLINK_VERSION_H
#define TINYLINK_VERSION_H

#define TINYLINK_VERSION_MAJOR 0
#define TINYLINK_VERSION_MINOR 4
#define TINYLINK_VERSION_PATCH 0

#define TINYLINK_VERSION_ENCODE(major, minor, patch)           \
    (((major) * 10000) + ((minor) * 100) + (patch))

#define TINYLINK_VERSION                                      \
    TINYLINK_VERSION_ENCODE(                                  \
        TINYLINK_VERSION_MAJOR,                               \
        TINYLINK_VERSION_MINOR,                               \
        TINYLINK_VERSION_PATCH)

#define TINYLINK_STRINGIFY_IMPL(value) #value
#define TINYLINK_STRINGIFY(value) TINYLINK_STRINGIFY_IMPL(value)

#define TINYLINK_VERSION_STR                                  \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MAJOR) "."            \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MINOR) "."            \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_PATCH)

#define TINYLINK_PROTOCOL_VERSION 1

static_assert(
    TINYLINK_VERSION_MINOR < 100,
    "TinyLink minor version must be less than 100");

static_assert(
    TINYLINK_VERSION_PATCH < 100,
    "TinyLink patch version must be less than 100");

#endif // TINYLINK_VERSION_H
```

If protocol compatibility ranges are introduced, replace the single protocol macro with clearly named minimum and maximum supported versions.

---

# Required test suite

## Current value consistency

- [ ] `TINYLINK_VERSION_MAJOR == 0`.
- [ ] `TINYLINK_VERSION_MINOR == 4`.
- [ ] `TINYLINK_VERSION_PATCH == 0`.
- [ ] `TINYLINK_VERSION == 400`.
- [ ] `TINYLINK_VERSION_STR` equals `"0.4.0"`.

## Encoding helper

Verify:

```text
0.0.0    -> 0
0.4.0    -> 400
1.0.0    -> 10000
1.2.3    -> 10203
12.34.56 -> 123456
```

## Compile-time comparisons

- [ ] `0.4.0 >= 0.3.9`.
- [ ] `0.4.0 < 0.4.1`.
- [ ] `1.0.0 > 0.99.99`.
- [ ] Feature-check examples compile as intended.

## String generation

- [ ] Single-digit components produce the expected string.
- [ ] Two-digit components produce the expected string.
- [ ] The result remains a compile-time string literal.
- [ ] Changing components automatically updates the string.

## Range validation

- [ ] Minor `99` is accepted.
- [ ] Patch `99` is accepted.
- [ ] Minor `100` is rejected.
- [ ] Patch `100` is rejected.

## Protocol separation

- [ ] A library patch update does not change protocol version automatically.
- [ ] A wire-breaking change requires an explicit protocol-version update.
- [ ] Handshake exchanges protocol version, not library release integer.
- [ ] Multiple library releases can share one protocol version.
- [ ] Incompatible protocol versions do not connect.
- [ ] Compatible protocol versions connect according to the documented policy.

## Include guard

- [ ] Header can be included repeatedly without duplicate definitions.
- [ ] No repository code depends on `TINY_LINK_VERSION_H`.
- [ ] New guard follows the `TINYLINK_` convention.

---

# Implementation checklist

## Version source of truth

- [ ] Keep major, minor, and patch as the release-version source of truth.
- [ ] Derive `TINYLINK_VERSION` from the components.
- [ ] Derive `TINYLINK_VERSION_STR` from the components.
- [ ] Add a reusable version-encoding macro.

## Range and comparison behavior

- [ ] Require minor and patch values below 100.
- [ ] Document numeric comparison semantics.
- [ ] Replace unexplained comparison literals with the encoding helper where practical.
- [ ] Define prerelease and build-metadata policy.

## Protocol compatibility

- [ ] Introduce an independent protocol version.
- [ ] Update handshake payload to carry protocol compatibility.
- [ ] Define compatible and incompatible version behavior.
- [ ] Require explicit protocol-version review for every wire-breaking change.
- [ ] Keep library release changes independent from protocol changes.

## Consistency

- [ ] Rename the include guard to `TINYLINK_VERSION_H` after checking usages.
- [ ] Keep `@file Version.h` if source-root-relative paths are the standard.
- [ ] Retain macros for preprocessor feature detection.
- [ ] Avoid manually duplicated typed constants.

## Verification

- [ ] Add numeric consistency tests.
- [ ] Add encoding-helper tests.
- [ ] Add string-generation tests.
- [ ] Add range-check tests.
- [ ] Add compile-time comparison tests.
- [ ] Add protocol-separation tests.

---

# Definition of done

The `Version.h` remediation is complete when:

- [ ] Major, minor, and patch are the single source of truth for the library release.
- [ ] The combined integer is derived automatically.
- [ ] The string literal is derived automatically.
- [ ] Component ranges guarantee unambiguous numeric encoding.
- [ ] Library release version and protocol compatibility version are independent.
- [ ] Handshake compatibility uses the protocol version.
- [ ] Wire-breaking changes require an intentional protocol-version decision.
- [ ] Prerelease and build-metadata handling is documented.
- [ ] Version consistency, comparison, range, string, and protocol-separation tests pass.

---

# Verdict

`Version.h` is correct for the current `0.4.0` release and contains no local safety defect.

The main maintenance risk is drift because the release version is currently written manually in three forms:

```text
components
combined integer
string
```

The more important architectural requirement is to keep library release versioning separate from wire-protocol compatibility. The handshake must not use `TINYLINK_VERSION` as a substitute for a real protocol version.

## Next review target

The core protocol files provided so far are sufficient to begin reviewing tests against the identified invariants.

- **Project:** `petesramek/tiny-link`
- **Relative path:** the primary TinyLink unit test file covering handshake, ACK, framing, and timeouts
