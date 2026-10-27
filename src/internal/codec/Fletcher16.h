/**
 * @file internal/codec/Fletcher16.h
 * @brief TinyLink Fletcher-16 checksum implementation.
 *
 * TinyLink uses Fletcher-16 with both accumulators initialized to 0xFF,
 * canonical modulo-255 reduction, and a result whose high byte is sum2 and
 * low byte is sum1. Empty input is a protocol exception and returns 0xFFFF.
 *
 * Packet serialization is responsible for choosing the byte order used to
 * store the returned numeric checksum.
 */
#ifndef TINYLINK_INTERNAL_CODEC_FLETCHER16_H
#define TINYLINK_INTERNAL_CODEC_FLETCHER16_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {
    namespace codec {

        /**
         * @brief Maximum number of bytes processed between accumulator reductions.
         *
         * Reducing every 20 bytes keeps both intermediate accumulators within the
         * range of uint16_t for every possible byte value.
         */
        static const size_t FLETCHER16_REDUCTION_BLOCK_SIZE = 20;

        /**
         * @brief Compute the TinyLink Fletcher-16 checksum of a byte sequence.
         *
         * The selected algorithm has these protocol parameters:
         *
         * - Initial sum1: 0xFF
         * - Initial sum2: 0xFF
         * - Reduction modulus: 255
         * - Reduction interval: 20 bytes
         * - Result high byte: sum2
         * - Result low byte: sum1
         * - Empty input result: 0xFFFF
         *
         * When @p length is nonzero, @p data must point to at least @p length readable
         * bytes. A null pointer is valid only when @p length is zero.
         *
         * @param data Input bytes, or nullptr when @p length is zero.
         * @param length Number of input bytes to process.
         * @return Numeric Fletcher-16 checksum with sum2 in the high byte.
         */
        inline uint16_t fletcher16(
            const uint8_t* data,
            size_t length)
        {
            if (length == 0) {
                return 0xFFFFu;
            }

            uint16_t sum1 = 0xFFu;
            uint16_t sum2 = 0xFFu;

            while (length > 0) {
                size_t blockLength =
                    length > FLETCHER16_REDUCTION_BLOCK_SIZE
                    ? FLETCHER16_REDUCTION_BLOCK_SIZE
                    : length;

                length -= blockLength;

                do {
                    sum1 = static_cast<uint16_t>(
                        sum1 + *data++);

                    sum2 = static_cast<uint16_t>(
                        sum2 + sum1);
                } while (--blockLength > 0);

                sum1 = static_cast<uint16_t>(
                    sum1 % 255u);

                sum2 = static_cast<uint16_t>(
                    sum2 % 255u);
            }

            return static_cast<uint16_t>(
                (sum2 << 8) | sum1);
        }

    } // namespace codec
} // namespace tinylink

#endif // TINYLINK_INTERNAL_CODEC_FLETCHER16_H
