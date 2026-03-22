#ifndef TINYLINK_MESSAGETYPE_H
#define TINYLINK_MESSAGETYPE_H

#include <stdint.h>

namespace tinylink {

    // Wire message type identifiers used in TinyLink frames.
    // Stored as single bytes on the wire (ASCII codes).
    enum class MessageType : uint8_t {
        Data  = 'D',          /**< Data payload frame */
        Debug = 'g',          /**< Debug / diagnostic payload frame */
        Cmd   = 'C',          /**< Command frame (renamed from Req/'R') */
        Ack   = 'A',          /**< Combined ACK/NACK — carries a TinyStatus code */
        Done  = 'K'           /**< Done / end-of-session frame */
    };

    // Convert a wire byte to MessageType. Returns true on success.
    // NOTE: Legacy 'R' is no longer accepted; peers must use 'C' for commands.
    inline bool message_type_from_wire(uint8_t b, MessageType &out) {
        switch (b) {
            case static_cast<uint8_t>(MessageType::Data):  out = MessageType::Data;  return true;
            case static_cast<uint8_t>(MessageType::Debug): out = MessageType::Debug; return true;
            case static_cast<uint8_t>(MessageType::Cmd):   out = MessageType::Cmd;   return true;
            case static_cast<uint8_t>(MessageType::Ack):   out = MessageType::Ack;   return true;
            case static_cast<uint8_t>(MessageType::Done):  out = MessageType::Done;  return true;
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
            case MessageType::Data:  return "Data";
            case MessageType::Debug: return "Debug";
            case MessageType::Cmd:   return "Cmd";
            case MessageType::Ack:   return "Ack";
            case MessageType::Done:  return "Done";
            default:                 return "Unknown";
        }
    }

} // namespace tinylink

#endif // TINYLINK_MESSAGETYPE_H
