/**
 * @file Fletcher16.h
 * @brief Fletcher-16 checksum implementation.
 *
 * AVR-friendly – no STL dependencies.
 */

#ifndef TINY_FLETCHER16_H
#define TINY_FLETCHER16_H

#include <stdint.h>
#include <stddef.h>

namespace tinylink {
namespace codec {

/**
 * @brief Compute a Fletcher-16 checksum.
 *
 * Initial accumulators are 0xFF (matching existing TinyLink behavior).
 * Reduction is applied every 20 bytes.
 *
 * @param data Pointer to input data.
 * @param len  Number of bytes to process.
 * @return (sum2 << 8) | sum1
 */
inline uint16_t fletcher16(const uint8_t* data, size_t len) {
    uint16_t sum1 = 0xff, sum2 = 0xff;

    while (len) {
        size_t tlen = len > 20 ? 20 : len;
        len -= tlen;
        do {
            sum1 += *data++;
            sum2 += sum1;
        } while (--tlen);

        sum1 = (sum1 & 0xff) + (sum1 >> 8);
        sum2 = (sum2 & 0xff) + (sum2 >> 8);
    }

    return (sum2 << 8) | sum1;
}

} // namespace codec
} // namespace tinylink

#endif // TINY_FLETCHER16_H
