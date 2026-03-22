/**
 * @file CobsCodec.cpp
 * @brief COBS encode/decode implementation for TinyLink.
 *
 * Preserves original TinyLink semantics:
 *   - cobs_encode returns 0 when dstCap is insufficient.
 *   - cobs_decode returns 0 on invalid frames (zero code byte or out-of-bounds
 *     code pointer) and on dstCap overflow.
 */

#include "CobsCodec.h"

namespace tinylink {
namespace codec {

size_t cobs_encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
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

size_t cobs_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
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
