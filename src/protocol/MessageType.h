#ifndef TINYLINK_MESSAGETYPE_H
#define TINYLINK_MESSAGETYPE_H

#include <stdint.h>

namespace tinylink {

    // Wire message type identifiers used in TinyLink frames.
    // Stored as single bytes on the wire (ASCII codes).
    enum class MessageType : uint8_t {
        Data  = 'D',
        Debug = 'g',
        Cmd   = 'C',  // renamed from Req -> Cmd; new outgoing frames use 'C'
        Done  = 'K'
    };

    // Convert a wire byte to MessageType. Returns true on success.
    // Accept both legacy 'R' (Req) and new 'C' (Cmd) for migration.
    inline bool message_type_from_wire(uint8_t b, MessageType &out) {
        switch (b) {
            case static_cast<uint8_t>(MessageType::Data):  out = MessageType::Data;  return true;
            case static_cast<uint8_t>(MessageType::Debug): out = MessageType::Debug; return true;
            case static_cast<uint8_t>(MessageType::Cmd):   out = MessageType::Cmd;   return true;
            case 'R': /* legacy: Req */                    out = MessageType::Cmd;   return true;
            case static_cast<uint8_t>(MessageType::Done):  out = MessageType::Done;  return true;
            default: return false;
        }
    }

    // Small helper to get the wire byte for a MessageType.
    // Emits the new 'C' value for commands.
    inline uint8_t message_type_to_wire(MessageType t) {
        return static_cast<uint8_t>(t);
    }

    // to-string helper for diagnostics.
    inline const char* message_type_to_string(MessageType t) {
        switch (t) {
            case MessageType::Data:  return "Data";
            case MessageType::Debug: return "Debug";
            case MessageType::Cmd:   return "Cmd";
            case MessageType::Done:  return "Done";
            default:                 return "Unknown";
        }
    }

} // namespace tinylink

#endif // TINYLINK_MESSAGETYPE_H
