/**
 * @file protocol/internal/Packed.h
 * @brief Cross-compiler packed-struct attribute macros for TinyLink wire types.
 *
 * Usage:
 *   TINYLINK_PACKED_BEGIN
 *   struct Foo {
 *       uint8_t a;
 *       uint16_t b;
 *   } TINYLINK_PACKED;
 *   TINYLINK_PACKED_END
 *
 * Internal header: not part of the public API surface.
 */

#ifndef TINYLINK_INTERNAL_PACKED_H
#define TINYLINK_INTERNAL_PACKED_H

#if defined(_MSC_VER)
    #define TINYLINK_PACKED_BEGIN __pragma(pack(push, 1))
    #define TINYLINK_PACKED_END   __pragma(pack(pop))
    #define TINYLINK_PACKED
#else
    #define TINYLINK_PACKED_BEGIN
    #define TINYLINK_PACKED_END
    #define TINYLINK_PACKED __attribute__((packed))
#endif

#endif // TINYLINK_INTERNAL_PACKED_H
