/**
 * @file Fletcher16.cpp
 * @brief Fletcher-16 checksum implementation for TinyLink.
 *
 * Matches the behavior of the original inline TinyLink::fletcher16:
 *   - Initial accumulators: sum1 = 0xFF, sum2 = 0xFF.
 *   - Reduction (mod 255) applied after every 20 bytes.
 *   - Returns (sum2 << 8) | sum1.
 */

#include "Fletcher16.h"

namespace tinylink {
namespace codec {

uint16_t fletcher16(const uint8_t* data, size_t len) {
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
