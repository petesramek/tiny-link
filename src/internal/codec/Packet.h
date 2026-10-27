/**
 * @file internal/codec/Packet.h
 * @brief Explicit TinyLink plain-packet encoding and decoding.
 *
 * Wire layout:
 *
 * - Byte 0: application or reserved message type
 * - Byte 1: sequence number
 * - Byte 2: payload length
 * - Bytes 3 through 3 + payload length - 1: payload
 * - Final two bytes: Fletcher-16 checksum, low byte first
 *
 * The checksum covers the three-byte header and payload. COBS framing and the
 * zero stream delimiter are handled outside this codec. Source and destination
 * ranges must not overlap.
 */
#ifndef TINYLINK_INTERNAL_CODEC_PACKET_H
#define TINYLINK_INTERNAL_CODEC_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "internal/codec/Fletcher16.h"

namespace tinylink {
    namespace packet {

        /** @brief Number of bytes in the plain-packet header. */
        static const size_t PACKET_HEADER_SIZE = 3;

        /** @brief Number of bytes in the Fletcher-16 checksum field. */
        static const size_t PACKET_CHECKSUM_SIZE = 2;

        /** @brief Smallest valid plain packet, representing an empty payload. */
        static const size_t PACKET_MIN_SIZE =
            PACKET_HEADER_SIZE + PACKET_CHECKSUM_SIZE;

        /** @brief Maximum payload carried by the TinyLink wire protocol. */
        static const size_t PACKET_MAX_PAYLOAD_SIZE = 64;

        /** @brief Largest valid TinyLink plain packet. */
        static const size_t PACKET_MAX_SIZE =
            PACKET_HEADER_SIZE +
            PACKET_MAX_PAYLOAD_SIZE +
            PACKET_CHECKSUM_SIZE;

        static_assert(
            PACKET_MAX_PAYLOAD_SIZE <= UINT8_MAX,
            "TinyLink payload length must fit in one wire byte");

        static_assert(
            PACKET_MIN_SIZE == 5,
            "TinyLink minimum packet size changed");

        static_assert(
            PACKET_MAX_SIZE == 69,
            "TinyLink maximum packet size changed");

        /** @brief Result category for packet size, encoding, and decoding operations. */
        enum class PacketStatus : uint8_t {
            Success,
            InvalidArgument,
            PayloadTooLarge,
            FrameTooShort,
            LengthMismatch,
            ChecksumMismatch,
            OutputTooSmall
        };

        /** @brief Packet operation result with status and relevant length. */
        struct PacketResult {
            PacketStatus status; /**< Operation outcome. */
            size_t length;       /**< Packet or payload bytes produced on success. */
        };

        /**
         * @brief Calculate a TinyLink plain-packet size from its payload length.
         *
         * @param payloadLength Payload length in bytes.
         * @param packetSize Destination updated only when calculation succeeds.
         * @return Success or PayloadTooLarge.
         */
        inline PacketStatus packet_size(
            size_t payloadLength,
            size_t& packetSize)
        {
            if (payloadLength > PACKET_MAX_PAYLOAD_SIZE) {
                return PacketStatus::PayloadTooLarge;
            }

            packetSize =
                PACKET_HEADER_SIZE +
                payloadLength +
                PACKET_CHECKSUM_SIZE;

            return PacketStatus::Success;
        }

        /**
         * @brief Encode a semantic packet into the TinyLink plain wire format.
         *
         * Empty payloads are valid, and @p payload may then be nullptr. For a nonempty
         * payload, @p payload must be non-null. Destination contents remain unchanged
         * on failure. Payload and destination ranges must not overlap.
         *
         * @param type Raw application or reserved message-type byte.
         * @param sequence Packet sequence number.
         * @param payload Payload bytes, or nullptr for an empty payload.
         * @param payloadLength Number of payload bytes.
         * @param destination Destination packet buffer.
         * @param destinationCapacity Capacity of @p destination in bytes.
         * @return Status and number of encoded packet bytes.
         */
        inline PacketResult pack(
            uint8_t type,
            uint8_t sequence,
            const uint8_t* payload,
            size_t payloadLength,
            uint8_t* destination,
            size_t destinationCapacity)
        {
            if (!destination ||
                (!payload && payloadLength > 0)) {
                return { PacketStatus::InvalidArgument, 0 };
            }

            size_t required = 0;

            const PacketStatus sizeStatus =
                packet_size(payloadLength, required);

            if (sizeStatus != PacketStatus::Success) {
                return { sizeStatus, 0 };
            }

            if (destinationCapacity < required) {
                return { PacketStatus::OutputTooSmall, 0 };
            }

            uint8_t encoded[PACKET_MAX_SIZE] = {};

            encoded[0] = type;
            encoded[1] = sequence;
            encoded[2] =
                static_cast<uint8_t>(payloadLength);

            for (size_t index = 0;
                index < payloadLength;
                ++index) {
                encoded[PACKET_HEADER_SIZE + index] =
                    payload[index];
            }

            const size_t protectedLength =
                PACKET_HEADER_SIZE + payloadLength;

            const uint16_t checksum =
                tinylink::codec::fletcher16(
                    encoded,
                    protectedLength);

            encoded[protectedLength] =
                static_cast<uint8_t>(
                    checksum & 0xFFu);

            encoded[protectedLength + 1u] =
                static_cast<uint8_t>(
                    (checksum >> 8) & 0xFFu);

            for (size_t index = 0;
                index < required;
                ++index) {
                destination[index] = encoded[index];
            }

            return {
                PacketStatus::Success,
                required
            };
        }

        /**
         * @brief Decode and validate a TinyLink plain packet.
         *
         * Empty payloads are valid. Passing a null payload destination performs
         * validation and metadata extraction without copying payload bytes. Type,
         * sequence, payload, and payload length remain unchanged on failure. Source
         * and payload-destination ranges must not overlap.
         *
         * @param source Encoded plain packet.
         * @param sourceLength Number of source bytes.
         * @param type Optional decoded type destination.
         * @param sequence Optional decoded sequence destination.
         * @param payload Optional decoded payload destination.
         * @param payloadCapacity Capacity of @p payload in bytes.
         * @param payloadLength Optional decoded payload-length destination.
         * @return Status and decoded payload length.
         */
        inline PacketResult unpack(
            const uint8_t* source,
            size_t sourceLength,
            uint8_t* type,
            uint8_t* sequence,
            uint8_t* payload,
            size_t payloadCapacity,
            size_t* payloadLength)
        {
            if (!source) {
                return { PacketStatus::InvalidArgument, 0 };
            }

            if (sourceLength < PACKET_MIN_SIZE) {
                return { PacketStatus::FrameTooShort, 0 };
            }

            const size_t decodedPayloadLength =
                source[2];

            if (decodedPayloadLength >
                PACKET_MAX_PAYLOAD_SIZE) {
                return { PacketStatus::PayloadTooLarge, 0 };
            }

            size_t expectedSize = 0;

            const PacketStatus sizeStatus =
                packet_size(
                    decodedPayloadLength,
                    expectedSize);

            if (sizeStatus != PacketStatus::Success) {
                return { sizeStatus, 0 };
            }

            if (sourceLength != expectedSize) {
                return { PacketStatus::LengthMismatch, 0 };
            }

            if (payload &&
                payloadCapacity < decodedPayloadLength) {
                return { PacketStatus::OutputTooSmall, 0 };
            }

            const size_t protectedLength =
                PACKET_HEADER_SIZE +
                decodedPayloadLength;

            const uint16_t receivedChecksum =
                static_cast<uint16_t>(
                    source[protectedLength]) |
                static_cast<uint16_t>(
                    static_cast<uint16_t>(
                        source[protectedLength + 1u]) << 8);

            const uint16_t calculatedChecksum =
                tinylink::codec::fletcher16(
                    source,
                    protectedLength);

            if (receivedChecksum != calculatedChecksum) {
                return { PacketStatus::ChecksumMismatch, 0 };
            }

            if (type) {
                *type = source[0];
            }

            if (sequence) {
                *sequence = source[1];
            }

            if (payload) {
                for (size_t index = 0;
                    index < decodedPayloadLength;
                    ++index) {
                    payload[index] =
                        source[PACKET_HEADER_SIZE + index];
                }
            }

            if (payloadLength) {
                *payloadLength =
                    decodedPayloadLength;
            }

            return {
                PacketStatus::Success,
                decodedPayloadLength
            };
        }

    } // namespace packet
} // namespace tinylink

#endif // TINYLINK_INTERNAL_CODEC_PACKET_H
