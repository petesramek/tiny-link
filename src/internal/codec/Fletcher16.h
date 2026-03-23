/**
 * @file Fletcher16.h
 * @brief Fletcher-16 checksum declaration.
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
uint16_t fletcher16(const uint8_t* data, size_t len);

} // namespace codec
} // namespace tinylink

#endif // TINY_FLETCHER16_H
