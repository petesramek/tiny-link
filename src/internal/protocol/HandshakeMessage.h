/**
 * @file internal/protocol/HandshakeMessage.h
 * @brief Internal handshake payload encoding and decoding for TinyLink.
 *
 * The handshake payload contains a message kind and an independent wire-
 * protocol version. Both fields are validated before a handshake message is
 * accepted.
 *
 * Internal header: not part of the public API surface.
 */
#ifndef TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H
#define TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "Version.h"

namespace tinylink {

    /** @brief Size of an encoded handshake payload in bytes. */
    static const size_t HANDSHAKE_MESSAGE_WIRE_SIZE = 2;

    /**
     * @brief Kind of handshake message exchanged by TinyLink peers.
     *
     * Values are stable one-byte wire identifiers used only inside handshake
     * payloads.
     */
    enum class HandshakeKind : uint8_t {
        Request = 0x00,  /**< Starts or restarts a handshake exchange. */
        Response = 0x01  /**< Acknowledges a compatible handshake request. */
    };

    static_assert(sizeof(HandshakeKind) == 1,
        "HandshakeKind must have an 8-bit representation");

    static_assert(static_cast<uint8_t>(HandshakeKind::Request) == 0x00,
        "HandshakeKind::Request wire value changed");

    static_assert(static_cast<uint8_t>(HandshakeKind::Response) == 0x01,
        "HandshakeKind::Response wire value changed");

    static_assert(TINYLINK_PROTOCOL_VERSION <= UINT8_MAX,
        "TinyLink protocol version must fit in one wire byte");

    /**
     * @brief Validated semantic handshake payload.
     *
     * The message kind identifies the handshake phase. The protocol version
     * independently identifies the wire protocol spoken by the sender.
     */
    struct HandshakeMessage {
        HandshakeKind kind;      /**< Request or response handshake kind. */
        uint8_t protocolVersion; /**< Wire-protocol version advertised by the sender. */
    };

    /** @brief Result of encoding or decoding a handshake payload. */
    enum class HandshakeMessageCodecStatus : uint8_t {
        Success,
        InvalidArgument,
        InvalidLength,
        InvalidKind,
        UnsupportedVersion,
        OutputTooSmall
    };

    /**
     * @brief Determine whether a HandshakeKind object contains a declared value.
     *
     * @param kind Handshake kind to validate.
     * @return true when @p kind is Request or Response; otherwise false.
     */
    inline bool is_valid_handshake_kind(HandshakeKind kind)
    {
        switch (kind) {
        case HandshakeKind::Request:
        case HandshakeKind::Response:
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Convert a raw wire byte to a HandshakeKind.
     *
     * The destination remains unchanged when @p value is not a declared kind.
     *
     * @param value Raw handshake-kind byte.
     * @param kind Destination updated only when conversion succeeds.
     * @return true when @p value identifies a declared handshake kind.
     */
    inline bool handshake_kind_from_wire(
        uint8_t value,
        HandshakeKind& kind)
    {
        switch (value) {
        case static_cast<uint8_t>(HandshakeKind::Request):
            kind = HandshakeKind::Request;
            return true;

        case static_cast<uint8_t>(HandshakeKind::Response):
            kind = HandshakeKind::Response;
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Encode a validated handshake message into its wire payload.
     *
     * The output buffer remains unchanged when encoding fails. Only the current
     * TinyLink protocol version is emitted during this initial compatibility
     * model.
     *
     * @param message Semantic handshake message to encode.
     * @param output Destination buffer.
     * @param outputCapacity Capacity of @p output in bytes.
     * @return Codec status describing the result.
     */
    inline HandshakeMessageCodecStatus encode_handshake_message(
        const HandshakeMessage& message,
        uint8_t* output,
        size_t outputCapacity)
    {
        if (!output) {
            return HandshakeMessageCodecStatus::InvalidArgument;
        }

        if (outputCapacity < HANDSHAKE_MESSAGE_WIRE_SIZE) {
            return HandshakeMessageCodecStatus::OutputTooSmall;
        }

        if (!is_valid_handshake_kind(message.kind)) {
            return HandshakeMessageCodecStatus::InvalidKind;
        }

        if (message.protocolVersion != TINYLINK_PROTOCOL_VERSION) {
            return HandshakeMessageCodecStatus::UnsupportedVersion;
        }

        output[0] = static_cast<uint8_t>(message.kind);
        output[1] = message.protocolVersion;

        return HandshakeMessageCodecStatus::Success;
    }

    /**
     * @brief Decode and validate a handshake wire payload.
     *
     * The destination remains unchanged when decoding fails. A message is
     * accepted only when its kind is declared and its protocol version matches
     * TINYLINK_PROTOCOL_VERSION.
     *
     * @param input Encoded handshake payload.
     * @param inputLength Number of bytes in @p input.
     * @param message Destination updated only after complete validation.
     * @return Codec status describing the result.
     */
    inline HandshakeMessageCodecStatus decode_handshake_message(
        const uint8_t* input,
        size_t inputLength,
        HandshakeMessage& message)
    {
        if (!input) {
            return HandshakeMessageCodecStatus::InvalidArgument;
        }

        if (inputLength != HANDSHAKE_MESSAGE_WIRE_SIZE) {
            return HandshakeMessageCodecStatus::InvalidLength;
        }

        HandshakeKind kind = HandshakeKind::Request;

        if (!handshake_kind_from_wire(input[0], kind)) {
            return HandshakeMessageCodecStatus::InvalidKind;
        }

        if (input[1] != TINYLINK_PROTOCOL_VERSION) {
            return HandshakeMessageCodecStatus::UnsupportedVersion;
        }

        const HandshakeMessage decodedMessage = {
            kind,
            input[1]
        };

        message = decodedMessage;

        return HandshakeMessageCodecStatus::Success;
    }

} // namespace tinylink

#endif // TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H
