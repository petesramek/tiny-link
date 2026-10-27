/**
 * @file protocol/MessageType.h
 * @brief Reserved internal message types used by TinyLink frames.
 *
 * The packet-header type byte is an application-defined message identifier
 * unless it matches one of the reserved internal values declared here.
 * Application sends must reject reserved values so they cannot be interpreted
 * as TinyLink control frames by the receiving peer.
 */
#ifndef TINYLINK_MESSAGETYPE_H
#define TINYLINK_MESSAGETYPE_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Reserved internal frame types recognized by TinyLink.
     *
     * Values are stable one-byte ASCII wire identifiers. All other byte values are
     * available to application messages unless reserved by a future protocol
     * version.
     */
    enum class MessageType : uint8_t {
        Log = 'L', /**< Log or diagnostic payload frame. */
        Ack = 'A', /**< Acknowledgement payload frame. */
        Handshake = 'H'  /**< Connection handshake frame. */
    };

    static_assert(sizeof(MessageType) == 1,
        "MessageType must have an 8-bit representation");
    static_assert(static_cast<uint8_t>(MessageType::Log) == 'L',
        "MessageType::Log wire value changed");
    static_assert(static_cast<uint8_t>(MessageType::Ack) == 'A',
        "MessageType::Ack wire value changed");
    static_assert(static_cast<uint8_t>(MessageType::Handshake) == 'H',
        "MessageType::Handshake wire value changed");

    /**
     * @brief Determine whether a raw packet type is reserved by TinyLink.
     *
     * @param value Raw packet-header type byte.
     * @return true when @p value identifies an internal TinyLink frame.
     */
    inline bool is_reserved_message_type(uint8_t value)
    {
        switch (value) {
        case static_cast<uint8_t>(MessageType::Log):
        case static_cast<uint8_t>(MessageType::Ack):
        case static_cast<uint8_t>(MessageType::Handshake):
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Convert a raw wire byte to a reserved MessageType.
     *
     * The destination remains unchanged when @p value is not a reserved internal
     * message type.
     *
     * @param value Raw packet-header type byte.
     * @param type Destination updated only when conversion succeeds.
     * @return true when @p value identifies a reserved internal frame.
     */
    inline bool message_type_from_wire(uint8_t value, MessageType& type)
    {
        switch (value) {
        case static_cast<uint8_t>(MessageType::Log):
            type = MessageType::Log;
            return true;

        case static_cast<uint8_t>(MessageType::Ack):
            type = MessageType::Ack;
            return true;

        case static_cast<uint8_t>(MessageType::Handshake):
            type = MessageType::Handshake;
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Determine whether a MessageType object contains a declared value.
     *
     * @param type Message type to validate.
     * @return true when @p type is one of the declared internal frame types.
     */
    inline bool is_valid_message_type(MessageType type)
    {
        return is_reserved_message_type(static_cast<uint8_t>(type));
    }

    /**
     * @brief Convert a declared MessageType to its stable wire byte.
     *
     * Callers must supply a declared MessageType value. Raw bytes must be
     * validated with message_type_from_wire() before conversion to MessageType.
     *
     * @param type Declared internal message type.
     * @return Stable one-byte ASCII wire identifier.
     */
    inline uint8_t message_type_to_wire(MessageType type)
    {
        return static_cast<uint8_t>(type);
    }

    /**
     * @brief Convert a MessageType to a human-readable diagnostic string.
     *
     * This helper is diagnostic only and does not replace validation.
     *
     * @param type Message type to describe.
     * @return Static diagnostic string, or "Unknown" for an undeclared value.
     */
    inline const char* message_type_to_string(MessageType type)
    {
        switch (type) {
        case MessageType::Log:
            return "Log";

        case MessageType::Ack:
            return "Ack";

        case MessageType::Handshake:
            return "Handshake";

        default:
            return "Unknown";
        }
    }

} // namespace tinylink

#endif // TINYLINK_MESSAGETYPE_H
