/**
 * @file Version.h
 * @brief Library release and wire-protocol versions for TinyLink.
 *
 * The library release version supports preprocessor feature detection and
 * normalized major.minor.patch comparisons. It does not encode prerelease
 * identifiers, build metadata, source revision, or wire compatibility.
 *
 * TINYLINK_PROTOCOL_VERSION is independent from the library release version.
 * It must change only when the TinyLink wire protocol changes incompatibly.
 */
#ifndef TINYLINK_VERSION_H
#define TINYLINK_VERSION_H

 /** @brief Major component of the TinyLink library release version. */
#define TINYLINK_VERSION_MAJOR 0

/** @brief Minor component of the TinyLink library release version. */
#define TINYLINK_VERSION_MINOR 5

/** @brief Patch component of the TinyLink library release version. */
#define TINYLINK_VERSION_PATCH 0

/**
 * @brief Encode a normalized major.minor.patch version for comparison.
 *
 * The minor and patch components must each be in the range 0 through 99.
 *
 * @param major Major version component.
 * @param minor Minor version component.
 * @param patch Patch version component.
 */
#define TINYLINK_VERSION_ENCODE(major, minor, patch) \
    (((major) * 10000) + ((minor) * 100) + (patch))

 /**
  * @brief Encoded TinyLink library release version.
  *
  * Use TINYLINK_VERSION_ENCODE() in feature checks instead of unexplained
  * numeric literals.
  */
#define TINYLINK_VERSION \
    TINYLINK_VERSION_ENCODE( \
        TINYLINK_VERSION_MAJOR, \
        TINYLINK_VERSION_MINOR, \
        TINYLINK_VERSION_PATCH)

  /** @brief Internal single-stage macro stringification helper. */
#define TINYLINK_STRINGIFY_IMPL(value) #value

/** @brief Expand and stringify a preprocessor value. */
#define TINYLINK_STRINGIFY(value) TINYLINK_STRINGIFY_IMPL(value)

/** @brief TinyLink library release version as a compile-time string literal. */
#define TINYLINK_VERSION_STR \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MAJOR) "." \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_MINOR) "." \
    TINYLINK_STRINGIFY(TINYLINK_VERSION_PATCH)

/**
 * @brief Current TinyLink wire-protocol version.
 *
 * This value is exchanged by the handshake protocol. It is intentionally
 * independent from TINYLINK_VERSION so compatible library releases can share
 * the same wire protocol.
 */
#define TINYLINK_PROTOCOL_VERSION 1

static_assert(
    TINYLINK_VERSION_MINOR >= 0 && TINYLINK_VERSION_MINOR < 100,
    "TinyLink minor version must be between 0 and 99");

static_assert(
    TINYLINK_VERSION_PATCH >= 0 && TINYLINK_VERSION_PATCH < 100,
    "TinyLink patch version must be between 0 and 99");

static_assert(
    TINYLINK_PROTOCOL_VERSION > 0,
    "TinyLink protocol version must be greater than zero");

#endif // TINYLINK_VERSION_H
