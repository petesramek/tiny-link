/**
 * @file internal/codec/CobsCodec.h
 * @brief Allocation-free COBS encoding and decoding for TinyLink.
 *
 * The codec processes only COBS blocks. Stream delimiter bytes are added and
 * removed by the caller. Source and destination ranges must not overlap.
 */
#ifndef TINYLINK_INTERNAL_CODEC_COBSCODEC_H
#define TINYLINK_INTERNAL_CODEC_COBSCODEC_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {
    namespace codec {

        /** @brief Outcome of a COBS size, encode, or decode operation. */
        enum class CobsStatus : uint8_t {
            Success,
            InvalidArgument,
            InvalidEncoding,
            OutputTooSmall,
            SizeOverflow
        };

        /** @brief COBS operation result containing status and produced byte count. */
        struct CobsResult {
            CobsStatus status;   /**< Operation outcome. */
            size_t bytesWritten; /**< Produced bytes on success; otherwise zero. */
        };

        /**
         * @brief Calculate the maximum encoded size required for a source length.
         *
         * The returned capacity excludes stream delimiter bytes. A destination of this
         * size is sufficient for every source byte pattern of @p sourceLength.
         *
         * @param sourceLength Number of plain source bytes.
         * @param required Destination updated only when calculation succeeds.
         * @return CobsStatus::Success or CobsStatus::SizeOverflow.
         */
        inline CobsStatus cobs_encoded_size(
            size_t sourceLength,
            size_t& required)
        {
            const size_t overhead = sourceLength / 254u;
            const size_t maximum = static_cast<size_t>(-1);

            if (overhead >= maximum ||
                sourceLength > maximum - overhead - 1u) {
                return CobsStatus::SizeOverflow;
            }

            required = sourceLength + overhead + 1u;

            return CobsStatus::Success;
        }

        /**
         * @brief Encode a byte sequence using COBS.
         *
         * Empty source input is encoded canonically as one byte with value 0x01.
         * A null source is permitted only when @p sourceLength is zero. Destination
         * contents remain unchanged when this function fails. Source and destination
         * ranges must not overlap. No stream delimiter is written.
         *
         * @param source Source bytes, or nullptr when @p sourceLength is zero.
         * @param sourceLength Number of source bytes.
         * @param destination Destination buffer.
         * @param destinationCapacity Capacity of @p destination in bytes.
         * @return Operation status and encoded byte count.
         */
        inline CobsResult cobs_encode(
            const uint8_t* source,
            size_t sourceLength,
            uint8_t* destination,
            size_t destinationCapacity)
        {
            if ((!source && sourceLength > 0) ||
                !destination) {
                return { CobsStatus::InvalidArgument, 0 };
            }

            size_t required = 0;

            const CobsStatus sizeStatus =
                cobs_encoded_size(sourceLength, required);

            if (sizeStatus != CobsStatus::Success) {
                return { sizeStatus, 0 };
            }

            if (destinationCapacity < required) {
                return { CobsStatus::OutputTooSmall, 0 };
            }

            size_t readIndex = 0;
            size_t writeIndex = 1;
            size_t codeIndex = 0;
            uint8_t code = 1;

            while (readIndex < sourceLength) {
                if (source[readIndex] == 0) {
                    destination[codeIndex] = code;
                    code = 1;
                    codeIndex = writeIndex++;
                }
                else {
                    destination[writeIndex++] = source[readIndex];
                    ++code;

                    if (code == 0xFFu) {
                        destination[codeIndex] = code;
                        code = 1;
                        codeIndex = writeIndex++;
                    }
                }

                ++readIndex;
            }

            destination[codeIndex] = code;

            return { CobsStatus::Success, writeIndex };
        }

        /**
         * @brief Validate a COBS block and calculate its decoded size.
         *
         * Encoded input excludes stream delimiter bytes. A zero byte anywhere in the
         * encoded block is invalid. Empty encoded input is invalid. Encoded empty
         * source is represented by the one-byte block 0x01.
         *
         * @param source COBS-encoded bytes.
         * @param sourceLength Number of encoded bytes.
         * @param decodedSize Destination updated only when validation succeeds.
         * @return Status describing validation or size-calculation outcome.
         */
        inline CobsStatus cobs_decoded_size(
            const uint8_t* source,
            size_t sourceLength,
            size_t& decodedSize)
        {
            if (!source || sourceLength == 0) {
                return CobsStatus::InvalidArgument;
            }

            size_t readIndex = 0;
            size_t required = 0;

            while (readIndex < sourceLength) {
                const uint8_t code = source[readIndex++];

                if (code == 0) {
                    return CobsStatus::InvalidEncoding;
                }

                const size_t copiedBytes =
                    static_cast<size_t>(code - 1u);

                if (copiedBytes > sourceLength - readIndex) {
                    return CobsStatus::InvalidEncoding;
                }

                const size_t insertedZero =
                    code < 0xFFu &&
                    readIndex + copiedBytes < sourceLength
                    ? 1u
                    : 0u;

                const size_t maximum = static_cast<size_t>(-1);

                if (required >
                    maximum - copiedBytes - insertedZero) {
                    return CobsStatus::SizeOverflow;
                }

                required += copiedBytes + insertedZero;
                readIndex += copiedBytes;
            }

            decodedSize = required;

            return CobsStatus::Success;
        }

        /**
         * @brief Decode a COBS block.
         *
         * The encoded source must not contain framing delimiters. Source and
         * destination ranges must not overlap. The destination remains unchanged when
         * validation fails or capacity is insufficient. A successful decode may
         * produce zero bytes, as with the encoded empty-source block 0x01.
         *
         * @param source COBS-encoded bytes with delimiters removed.
         * @param sourceLength Number of encoded bytes.
         * @param destination Destination buffer; may be nullptr only for zero output.
         * @param destinationCapacity Capacity of @p destination in bytes.
         * @return Operation status and decoded byte count.
         */
        inline CobsResult cobs_decode(
            const uint8_t* source,
            size_t sourceLength,
            uint8_t* destination,
            size_t destinationCapacity)
        {
            size_t required = 0;

            const CobsStatus validationStatus =
                cobs_decoded_size(
                    source,
                    sourceLength,
                    required);

            if (validationStatus != CobsStatus::Success) {
                return { validationStatus, 0 };
            }

            if (required > 0 && !destination) {
                return { CobsStatus::InvalidArgument, 0 };
            }

            if (destinationCapacity < required) {
                return { CobsStatus::OutputTooSmall, 0 };
            }

            size_t readIndex = 0;
            size_t writeIndex = 0;

            while (readIndex < sourceLength) {
                const uint8_t code = source[readIndex++];

                const size_t copiedBytes =
                    static_cast<size_t>(code - 1u);

                for (size_t index = 0;
                    index < copiedBytes;
                    ++index) {
                    destination[writeIndex++] =
                        source[readIndex++];
                }

                if (code < 0xFFu &&
                    readIndex < sourceLength) {
                    destination[writeIndex++] = 0;
                }
            }

            return { CobsStatus::Success, writeIndex };
        }

    } // namespace codec
} // namespace tinylink

#endif // TINYLINK_INTERNAL_CODEC_COBSCODEC_H
