/**
 * @file CobsCodec.h
 * @brief COBS (Consistent Overhead Byte Stuffing) encode/decode implementation.
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
inline size_t cobs_encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    // Worst-case encoded length: srcLen + (srcLen / 254) + 1
    // A safe upper bound is srcLen + 2 (holds for srcLen <= 253).
    // For larger inputs the +1 per 254-byte block is handled by the
    // overflow check inside the loop.
    if (dstCap == 0) return 0;

    size_t read_idx = 0, write_idx = 1, code_idx = 0;
    uint8_t code = 1;

    while (read_idx < srcLen) {
        if (src[read_idx] == 0) {
            if (code_idx >= dstCap) return 0;
            dst[code_idx] = code;
            code = 1;
            code_idx = write_idx;
            if (write_idx >= dstCap) return 0;
            write_idx++;
        } else {
            if (write_idx >= dstCap) return 0;
            dst[write_idx++] = src[read_idx];
            code++;
            if (code == 0xFF) {
                if (code_idx >= dstCap) return 0;
                dst[code_idx] = code;
                code = 1;
                code_idx = write_idx;
                if (write_idx >= dstCap) return 0;
                write_idx++;
            }
        }
        read_idx++;
    }

    if (code_idx >= dstCap) return 0;
    dst[code_idx] = code;
    return write_idx;
}

/**
 * @brief Decode a COBS-encoded byte buffer.
 *
 * @param src    Pointer to COBS-encoded data (no framing 0x00 delimiters).
 * @param srcLen Number of encoded bytes.
 * @param dst    Destination buffer.
 * @param dstCap Capacity of dst in bytes.
 * @return Number of bytes written to dst, or 0 on invalid frame / buffer overflow.
 */
inline size_t cobs_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    size_t read_idx = 0, write_idx = 0;

    while (read_idx < srcLen) {
        uint8_t code = src[read_idx++];

        if (code == 0 || (read_idx + code - 1) > srcLen)
            return 0;  // Invalid frame

        for (uint8_t i = 1; i < code; i++) {
            if (write_idx >= dstCap) return 0;
            dst[write_idx++] = src[read_idx++];
        }

        if (code < 0xFF && read_idx < srcLen) {
            if (write_idx >= dstCap) return 0;
            dst[write_idx++] = 0;
        }
    }

    return write_idx;
}

} // namespace codec
} // namespace tinylink

#endif // TINY_COBS_CODEC_H
