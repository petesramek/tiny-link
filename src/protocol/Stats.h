/**
 * @file Stats.h
 * @brief TinyStats — explicit per-status telemetry counters.
 *
 * BREAKING CHANGE: replaces the legacy {packets, crcErrs, timeouts} struct.
 * Each TinyStatus value now has a dedicated named counter.
 * Use increment(TinyStatus) to update and direct field access or countFor()
 * to read.
 */

#ifndef TINYLINK_STATS_H
#define TINYLINK_STATS_H

#include <stdint.h>
#include <stdio.h>
#include "protocol/Status.h"

namespace tinylink {

    /**
     * @brief Explicit per-status telemetry counters (BREAKING).
     *
     * One counter per TinyStatus value:
     *   packets    — STATUS_OK  (successfully received packets)
     *   crc        — ERR_CRC
     *   timeout    — ERR_TIMEOUT
     *   overflow   — ERR_OVERFLOW
     *   busy       — ERR_BUSY
     *   processing — ERR_PROCESSING
     *   unknown    — ERR_UNKNOWN / catch-all
     */
    struct TinyStats {
        typedef uint32_t CountType;

        CountType packets;    /**< Packets received without error (STATUS_OK) */
        CountType crc;        /**< Fletcher-16 / COBS framing failures (ERR_CRC) */
        CountType timeout;    /**< Inter-byte timeouts (ERR_TIMEOUT) */
        CountType overflow;   /**< Receive buffer overflows (ERR_OVERFLOW) */
        CountType busy;       /**< Peer-busy rejections (ERR_BUSY) */
        CountType processing; /**< Peer still processing (ERR_PROCESSING) */
        CountType unknown;    /**< Unspecified errors (ERR_UNKNOWN / catch-all) */

        TinyStats()
            : packets(0), crc(0), timeout(0), overflow(0),
              busy(0), processing(0), unknown(0) {}

        /** @brief Reset all counters to zero. */
        inline void clear() {
            packets = crc = timeout = overflow = busy = processing = unknown = 0;
        }

        /** @brief Increment the counter corresponding to status @p s. */
        inline void increment(TinyStatus s) {
            switch (s) {
                case TinyStatus::STATUS_OK:      ++packets;    break;
                case TinyStatus::ERR_CRC:        ++crc;        break;
                case TinyStatus::ERR_TIMEOUT:    ++timeout;    break;
                case TinyStatus::ERR_OVERFLOW:   ++overflow;   break;
                case TinyStatus::ERR_BUSY:       ++busy;       break;
                case TinyStatus::ERR_PROCESSING: ++processing; break;
                case TinyStatus::ERR_UNKNOWN:    ++unknown;    break;
                default:                         ++unknown;    break;
            }
        }

        /** @brief Return the counter for a specific status. */
        inline CountType countFor(TinyStatus s) const {
            switch (s) {
                case TinyStatus::STATUS_OK:      return packets;
                case TinyStatus::ERR_CRC:        return crc;
                case TinyStatus::ERR_TIMEOUT:    return timeout;
                case TinyStatus::ERR_OVERFLOW:   return overflow;
                case TinyStatus::ERR_BUSY:       return busy;
                case TinyStatus::ERR_PROCESSING: return processing;
                case TinyStatus::ERR_UNKNOWN:    return unknown;
                default:                         return unknown;
            }
        }

        /** @brief Add counters from @p o into this instance. */
        inline void merge(const TinyStats& o) {
            packets    += o.packets;
            crc        += o.crc;
            timeout    += o.timeout;
            overflow   += o.overflow;
            busy       += o.busy;
            processing += o.processing;
            unknown    += o.unknown;
        }

        /**
         * @brief Write a human-readable summary into @p buf.
         * @return Number of characters written (excluding NUL), or 0 on error.
         */
        inline int to_string(char* buf, int bufSize) const {
            if (bufSize <= 0) return 0;
            int n = snprintf(buf, static_cast<size_t>(bufSize),
                "ok=%u crc=%u tmo=%u ovf=%u busy=%u proc=%u unk=%u",
                static_cast<unsigned>(packets),
                static_cast<unsigned>(crc),
                static_cast<unsigned>(timeout),
                static_cast<unsigned>(overflow),
                static_cast<unsigned>(busy),
                static_cast<unsigned>(processing),
                static_cast<unsigned>(unknown));
            if (n < 0) return 0;
            return (n >= bufSize) ? (bufSize - 1) : n;
        }
    };

} // namespace tinylink

#endif // TINYLINK_STATS_H
