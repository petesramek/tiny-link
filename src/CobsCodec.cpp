#include "CobsCodec.h"

namespace tinylink {
namespace codec {

size_t cobs_encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    if (!dst || (!src && srcLen > 0) || dstCap == 0) return 0;

    size_t read_idx = 0;
    size_t write_idx = 1; // leave space for first code byte
    size_t code_idx = 0;  // index where current code will be written
    uint8_t code = 1;

    while (read_idx < srcLen) {
        uint8_t c = src[read_idx++];
        if (c == 0) {
            if (code_idx >= dstCap) return 0;
            dst[code_idx] = code;
            code = 1;
            if (write_idx >= dstCap) return 0;
            code_idx = write_idx++;
        } else {
            if (write_idx >= dstCap) return 0;
            dst[write_idx++] = c;
            code++;
            if (code == 0xFF) {
                if (code_idx >= dstCap) return 0;
                dst[code_idx] = code;
                code = 1;
                if (write_idx >= dstCap) return 0;
                code_idx = write_idx++;
            }
        }
    }

    if (code_idx >= dstCap) return 0;
    dst[code_idx] = code;

    return write_idx <= dstCap ? write_idx : 0;
}

size_t cobs_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
    if (!dst || (!src && srcLen > 0) || dstCap == 0) return 0;

    size_t read_idx = 0;
    size_t write_idx = 0;

    while (read_idx < srcLen) {
        uint8_t code = src[read_idx++];

        if (code == 0 || (read_idx + (size_t)code - 1) > srcLen) {
            return 0;
        }

        for (uint8_t i = 1; i < code; ++i) {
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
