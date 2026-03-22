/**
 * @file Stats.h
 * @brief TinyStats — telemetry counters for monitoring link quality.
 */

#ifndef TINYLINK_STATS_H
#define TINYLINK_STATS_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Telemetry counters for monitoring link quality.
     *
     * All counters are uint16/uint32, safe for embedded wrap-around.
     */
    struct TinyStats {
        uint32_t packets;   /**< Count of verified packets received */
        uint16_t crcErrs;   /**< Count of Fletcher-16 or framing failures */
        uint16_t timeouts;  /**< Count of inter-byte timeouts */

        TinyStats() : packets(0), crcErrs(0), timeouts(0) {}

        /** @brief Reset all counters to zero. */
        inline void clear() { packets = 0; crcErrs = 0; timeouts = 0; }
    };

} // namespace tinylink

#endif // TINYLINK_STATS_H
