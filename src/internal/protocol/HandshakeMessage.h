/**
 * @file internal/HandshakeMessage.h
 * @brief HandshakeMessage — packed handshake payload for MessageType::Handshake ('H') frames.
 *
 * Internal header: not part of the public API surface. Users should not
 * depend on the wire layout defined here.
 */

#ifndef TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H
#define TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H

#include <stdint.h>
#include "internal/protocol/Packed.h"

namespace tinylink {

    /**
     * @brief One-byte handshake payload for MessageType::Handshake ('H') frames.
     *
     * @param version  Protocol version advertised by the sender (currently 0).
     */
    TINYLINK_PACKED_BEGIN
    struct HandshakeMessage {
        uint8_t version; /**< Protocol version (0 = initial) */
    } TINYLINK_PACKED;
    TINYLINK_PACKED_END

    static_assert(sizeof(HandshakeMessage) == 1, "HandshakeMessage must be exactly 1 byte");

} // namespace tinylink

#endif // TINYLINK_INTERNAL_HANDSHAKEMESSAGE_H
