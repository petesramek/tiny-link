#ifndef TINYLINK_MESSAGETYPE_H
#define TINYLINK_MESSAGETYPE_H

#include <stdint.h>

namespace tinylink {

    // Wire message type identifiers used in TinyLink frames.
    // Stored as single bytes on the wire (ASCII codes).
    enum class MessageType : uint8_t {
        Data      = 'D',      /**< Data payload frame */
        Log       = 'L',      /**< Log / diagnostic payload frame */
        Cmd       = 'C',      /**< Command frame (renamed from Req/'R') */
        Ack       = 'A',      /**< Combined ACK/NACK — carries a TinyStatus code */
        Handshake = 'H'       /**< Handshake frame — sent at startup to establish a connection */
    };

    // Convert a wire byte to MessageType. Returns true on success.
    // NOTE: Legacy 'R' is no longer accepted; peers must use 'C' for commands.
    inline bool message_type_from_wire(uint8_t b, MessageType &out) {
        switch (b) {
            case static_cast<uint8_t>(MessageType::Data):      out = MessageType::Data;      return true;
            case static_cast<uint8_t>(MessageType::Log):       out = MessageType::Log;       return true;
            case static_cast<uint8_t>(MessageType::Cmd):       out = MessageType::Cmd;       return true;
            case static_cast<uint8_t>(MessageType::Ack):       out = MessageType::Ack;       return true;
            case static_cast<uint8_t>(MessageType::Done):      out = MessageType::Done;      return true;
            case static_cast<uint8_t>(MessageType::Handshake): out = MessageType::Handshake; return true;
            default: return false;
        }
    }

    // Small helper to get the wire byte for a MessageType.
    inline uint8_t message_type_to_wire(MessageType t) {
        return static_cast<uint8_t>(t);
    }

    // to-string helper for diagnostics.
    inline const char* message_type_to_string(MessageType t) {
        switch (t) {
            case MessageType::Data:      return "Data";
            case MessageType::Log:       return "Log";
            case MessageType::Cmd:       return "Cmd";
            case MessageType::Ack:       return "Ack";
            case MessageType::Done:      return "Done";
            case MessageType::Handshake: return "Handshake";
            default:                     return "Unknown";
        }
    }

} // namespace tinylink

#endif // TINYLINK_MESSAGETYPE_H
