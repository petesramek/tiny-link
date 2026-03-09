/**
 * @file TinyProtocol.h
 * @brief Core constants, enums, and telemetry structures for TinyLink.
 */

#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include <stdint.h>
#include <string.h>

namespace tinylink {

    /** @name Message Types */
    /** @{ */
    const uint8_t TYPE_DATA  = 'D'; /**< Standard data payload */
    const uint8_t TYPE_DEBUG = 'g'; /**< Debugging/Log information */
    const uint8_t TYPE_REQ   = 'R'; /**< Request for data/action */
    const uint8_t TYPE_DONE  = 'K'; /**< Acknowledgment of completion */
    /** @} */

    /**
     * @brief High-level status codes for the TinyLink engine.
     */
    enum class TinyStatus { 
        STATUS_OK = 0, /**< No errors detected */
        ERR_TIMEOUT,   /**< Inter-byte timeout triggered */
        ERR_CRC        /**< Fletcher-16 or COBS validation failure */
    };

    /**
     * @brief Internal state machine stages for COBS frame processing.
     */
    enum class TinyState { 
        WAIT_FOR_SYNC,  /**< Searching for the 0x00 frame delimiter */
        IN_FRAME        /**< Accumulating encoded bytes until next 0x00 */
    };

    /**
     * @brief Telemetry data for monitoring link quality and performance.
     */
    struct TinyStats {
        uint32_t packets;  /**< Count of successfully verified packets received */
        uint16_t crcErrs;  /**< Count of Fletcher-16 or COBS framing failures */
        uint16_t timeouts; /**< Count of frames that were started but timed out */
    };
}

#endif // TINY_PROTOCOL_H
