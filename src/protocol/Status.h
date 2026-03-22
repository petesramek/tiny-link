/**
 * @file Status.h
 * @brief TinyStatus enum — ACK/error codes carried in TinyAck frames.
 */

#ifndef TINYLINK_STATUS_H
#define TINYLINK_STATUS_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Granular ACK and error codes used in TinyAck payload frames.
     *
     * These codes are transmitted on the wire inside a TinyAck struct
     * (MessageType::Ack / 'A').  Values are fixed; do not reorder.
     */
    enum class TinyStatus : uint8_t {
        STATUS_OK      = 0x00,  /**< No error; operation succeeded */
        ERR_CRC        = 0x01,  /**< Fletcher-16 checksum or COBS framing failure */
        ERR_TIMEOUT    = 0x02,  /**< Inter-byte timeout triggered */
        ERR_OVERFLOW   = 0x03,  /**< Receive buffer overflow */
        ERR_BUSY       = 0x04,  /**< Peer is busy; retry later */
        ERR_PROCESSING = 0x05,  /**< Peer is still processing a prior command */
        ERR_UNKNOWN    = 0xFF   /**< Unspecified / unrecognised error */
    };

    /** @brief Convert a TinyStatus code to a human-readable string. */
    inline const char* tinystatus_to_string(TinyStatus s) {
        switch (s) {
            case TinyStatus::STATUS_OK:      return "OK";
            case TinyStatus::ERR_CRC:        return "ERR_CRC";
            case TinyStatus::ERR_TIMEOUT:    return "ERR_TIMEOUT";
            case TinyStatus::ERR_OVERFLOW:   return "ERR_OVERFLOW";
            case TinyStatus::ERR_BUSY:       return "ERR_BUSY";
            case TinyStatus::ERR_PROCESSING: return "ERR_PROCESSING";
            case TinyStatus::ERR_UNKNOWN:    return "ERR_UNKNOWN";
            default:                         return "INVALID";
        }
    }

} // namespace tinylink

#endif // TINYLINK_STATUS_H
