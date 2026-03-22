/**
 * @file TinyProtocol.h
 * @brief Core constants, enums, and telemetry structures for TinyLink.
 */

#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include <stdint.h>

#include "protocol/MessageType.h"

namespace tinylink {

    // ------------------------------------------------------------
    // High-Level Status Codes
    // ------------------------------------------------------------

    /**
     * @brief High-level error & status codes for TinyLink engine.
     */
    enum class TinyStatus : uint8_t {
        STATUS_OK = 0,   /**< No errors detected */
        ERR_TIMEOUT,     /**< Inter-byte timeout triggered */
        ERR_CRC          /**< Fletcher-16 checksum or COBS framing failure */
    };

    // ------------------------------------------------------------
    // COBS Decode State Machine
    // ------------------------------------------------------------

    /**
     * @brief Internal state stages for COBS frame processing.
     */
    enum class TinyState : uint8_t {
        WAIT_FOR_SYNC,   /**< Waiting for 0x00 frame delimiter */
        IN_FRAME         /**< Accumulating encoded bytes until next 0x00 */
    };

    // ------------------------------------------------------------
    // Telemetry & Diagnostics
    // ------------------------------------------------------------

    /**
     * @brief Telemetry counters for monitoring link quality.
     *
     * All counters are uint16/uint32, safe for embedded wrap-around.
     */
    struct TinyStats {
        uint32_t packets;   /**< Count of verified packets received */
        uint16_t crcErrs;   /**< Count of Fletcher-16 or framing failures */
        uint16_t timeouts;  /**< Count of inter-byte timeouts */

        TinyStats()
            : packets(0), crcErrs(0), timeouts(0)
        {
        }
    };

} // namespace tinylink

#endif // TINY_PROTOCOL_H
