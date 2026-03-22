/**
 * @file CobsCodec.h
 * @brief COBS (Consistent Overhead Byte Stuffing) encode/decode API.
 *
 * AVR-friendly – no STL dependencies.
 */

#ifndef TINY_COBS_CODEC_H
#define TINY_COBS_CODEC_H

#include <stdint.h>
#include <stddef.h>

namespace tinylink {
namespace codec {

/**
 * @brief Encode a byte buffer using COBS.
 *
 * @param src    Pointer to source data.
 * @param srcLen Number of bytes to encode.
 * @param dst    Destination buffer (must not overlap src).
 * @param dstCap Capacity of dst in bytes.
 * @return Number of bytes written to dst, or 0 on error (dstCap too small).
 */
size_t cobs_encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap);

/**
 * @brief Decode a COBS-encoded byte buffer.
 *
 * @param src    Pointer to COBS-encoded data (no framing 0x00 delimiters).
 * @param srcLen Number of encoded bytes.
 * @param dst    Destination buffer.
 * @param dstCap Capacity of dst in bytes.
 * @return Number of bytes written to dst, or 0 on invalid frame / buffer overflow.
 */
size_t cobs_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap);

} // namespace codec
} // namespace tinylink

#endif // TINY_COBS_CODEC_H
