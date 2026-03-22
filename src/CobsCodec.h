#ifndef TINYLINK_COBS_CODEC_H
#define TINYLINK_COBS_CODEC_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {
namespace codec {

// Encode src[0..srcLen-1] into dst (dstCap bytes available).
// Returns number of bytes written to dst, or 0 on error (dstCap too small).
size_t cobs_encode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap);

// Decode src (COBS format) into dst. Returns decoded length, or 0 on error.
size_t cobs_decode(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap);

} // namespace codec
} // namespace tinylink

#endif // TINYLINK_COBS_CODEC_H