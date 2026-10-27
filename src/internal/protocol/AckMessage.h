/**
 * @file internal/protocol/AckMessage.h
 * @brief Internal ACK payload encoding and decoding for TinyLink.
 *
 * The ACK payload contains only the result reported by the peer. The sequence
 * number in the packet header identifies the acknowledged application frame.
 *
 * Internal header: not part of the public API surface.
 */
#ifndef TINYLINK_INTERNAL_ACKMESSAGE_H
#define TINYLINK_INTERNAL_ACKMESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "protocol/AckResult.h"

namespace tinylink {

    /** @brief Size of an encoded ACK payload in bytes. */
    static const size_t ACK_MESSAGE_WIRE_SIZE = 1;

    /** @brief Result of encoding or decoding an ACK payload. */
    enum class AckMessageCodecStatus : uint8_t {
        Success,
        InvalidArgument,
        InvalidLength,
        InvalidResult,
        OutputTooSmall
    };

    /**
     * @brief Encode an ACK result into its one-byte wire payload.
     *
     * The output buffer remains unchanged when encoding fails.
     *
     * @param result ACK result to encode.
     * @param output Destination buffer.
     * @param outputCapacity Capacity of @p output in bytes.
     * @return Codec status describing the result.
     */
    inline AckMessageCodecStatus encode_ack_message(
        AckResult result,
        uint8_t* output,
        size_t outputCapacity)
    {
        if (!output) {
            return AckMessageCodecStatus::InvalidArgument;
        }

        if (outputCapacity < ACK_MESSAGE_WIRE_SIZE) {
            return AckMessageCodecStatus::OutputTooSmall;
        }

        const uint8_t rawResult = ack_result_to_wire(result);
        AckResult validatedResult = AckResult::Unknown;

        if (!ack_result_from_wire(rawResult, validatedResult)) {
            return AckMessageCodecStatus::InvalidResult;
        }

        output[0] = rawResult;
        return AckMessageCodecStatus::Success;
    }

    /**
     * @brief Decode a one-byte ACK payload.
     *
     * The destination remains unchanged when decoding fails.
     *
     * @param input Encoded ACK payload.
     * @param inputLength Number of bytes in @p input.
     * @param result Destination updated only after complete validation.
     * @return Codec status describing the result.
     */
    inline AckMessageCodecStatus decode_ack_message(
        const uint8_t* input,
        size_t inputLength,
        AckResult& result)
    {
        if (!input) {
            return AckMessageCodecStatus::InvalidArgument;
        }

        if (inputLength != ACK_MESSAGE_WIRE_SIZE) {
            return AckMessageCodecStatus::InvalidLength;
        }

        AckResult decodedResult = AckResult::Unknown;

        if (!ack_result_from_wire(input[0], decodedResult)) {
            return AckMessageCodecStatus::InvalidResult;
        }

        result = decodedResult;
        return AckMessageCodecStatus::Success;
    }

} // namespace tinylink

#endif // TINYLINK_INTERNAL_ACKMESSAGE_H
