/**
 * @file internal/AckMessage.h
 * @brief TinyAck — packed ACK/NACK payload carried in MessageType::Ack frames.
 *
 * Internal header: not part of the public API surface. Users should not
 * depend on the wire layout defined here.
 */

#ifndef TINYLINK_INTERNAL_ACKMESSAGE_H
#define TINYLINK_INTERNAL_ACKMESSAGE_H

#include <stdint.h>
#include "protocol/Status.h"

namespace tinylink {

#if defined(_MSC_VER)
    #define TINYLINK_PACKED_BEGIN __pragma(pack(push, 1))
    #define TINYLINK_PACKED_END   __pragma(pack(pop))
    #define TINYLINK_PACKED
#else
    #define TINYLINK_PACKED_BEGIN
    #define TINYLINK_PACKED_END
    #define TINYLINK_PACKED __attribute__((packed))
#endif

    /**
     * @brief Two-byte ACK/NACK payload for MessageType::Ack ('A') frames.
     *
     * @param seq    Sequence number being acknowledged (mirrors the original frame's seq).
     * @param result Status code indicating success or the type of error.
     */
    TINYLINK_PACKED_BEGIN
    struct TinyAck {
        uint8_t   seq;     /**< Mirrored sequence number from the original frame */
        TinyStatus result; /**< ACK/NACK result code */
    } TINYLINK_PACKED;
    TINYLINK_PACKED_END

    static_assert(sizeof(TinyAck) == 2, "TinyAck must be exactly 2 bytes");

} // namespace tinylink

#endif // TINYLINK_INTERNAL_ACKMESSAGE_H
