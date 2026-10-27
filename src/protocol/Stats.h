/**
 * @file protocol/Stats.h
 * @brief Local telemetry counters recorded by TinyLink.
 *
 * Stats is a copyable telemetry snapshot. It records local protocol, codec,
 * transport, and state-machine events classified by TelemetryEvent. Peer ACK
 * results are not counted as local telemetry events.
 */
#ifndef TINYLINK_STATS_H
#define TINYLINK_STATS_H

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "protocol/TelemetryEvent.h"

namespace tinylink {

    /**
     * @brief Copyable snapshot of local TinyLink telemetry counters.
     *
     * Counters saturate at UINT32_MAX. Incrementing or merging a saturated
     * counter leaves it at UINT32_MAX rather than wrapping to zero.
     *
     * This type does not provide synchronization. TinyLink must create snapshots
     * in the execution context or critical section that owns the live counters.
     */
    struct Stats {
        typedef uint32_t CountType;

        CountType packetsReceived;         /**< Application packets received successfully. */
        CountType cobsDecodeFailures;      /**< COBS decoding failures. */
        CountType checksumMismatches;      /**< Packet checksum mismatches. */
        CountType packetLengthMismatches;  /**< Packet lengths inconsistent with their headers. */
        CountType payloadSizeMismatches;   /**< Payloads incompatible with their destinations. */
        CountType packetEncodeFailures;    /**< Failures while encoding plain packets. */
        CountType cobsEncodeFailures;      /**< Failures while COBS-encoding packets. */
        CountType interByteTimeouts;       /**< Timeouts while receiving partial frames. */
        CountType ackTimeouts;             /**< Timeouts while awaiting acknowledgements. */
        CountType receiveOverflows;        /**< Incoming frames exceeding receive capacity. */
        CountType unexpectedMessageTypes; /**< Invalid or contextually unexpected frame types. */
        CountType unexpectedSequences;    /**< Received sequences not matching expectations. */
        CountType invalidAckResults;       /**< ACK payloads containing undefined results. */
        CountType invalidHandshakes;       /**< Malformed or unsupported handshake payloads. */

        /** @brief Construct a telemetry snapshot with every counter set to zero. */
        Stats()
            : packetsReceived(0),
            cobsDecodeFailures(0),
            checksumMismatches(0),
            packetLengthMismatches(0),
            payloadSizeMismatches(0),
            packetEncodeFailures(0),
            cobsEncodeFailures(0),
            interByteTimeouts(0),
            ackTimeouts(0),
            receiveOverflows(0),
            unexpectedMessageTypes(0),
            unexpectedSequences(0),
            invalidAckResults(0),
            invalidHandshakes(0)
        {}

        /** @brief Reset every telemetry counter to zero. */
        inline void clear()
        {
            packetsReceived = 0;
            cobsDecodeFailures = 0;
            checksumMismatches = 0;
            packetLengthMismatches = 0;
            payloadSizeMismatches = 0;
            packetEncodeFailures = 0;
            cobsEncodeFailures = 0;
            interByteTimeouts = 0;
            ackTimeouts = 0;
            receiveOverflows = 0;
            unexpectedMessageTypes = 0;
            unexpectedSequences = 0;
            invalidAckResults = 0;
            invalidHandshakes = 0;
        }

        /**
         * @brief Increment the counter corresponding to a telemetry event.
         *
         * @param event Local telemetry event to record.
         * @return true when @p event is declared; otherwise false.
         */
        inline bool increment(TelemetryEvent event)
        {
            CountType* counter = counter_for(event);

            if (!counter) {
                return false;
            }

            saturating_increment(*counter);
            return true;
        }

        /**
         * @brief Return the counter corresponding to a telemetry event.
         *
         * @param event Local telemetry event to query.
         * @return Counter value, or zero when @p event is undeclared.
         */
        inline CountType countFor(TelemetryEvent event) const
        {
            const CountType* counter = counter_for(event);
            return counter ? *counter : 0;
        }

        /**
         * @brief Merge another telemetry snapshot into this snapshot.
         *
         * Every counter uses saturating addition and therefore never wraps.
         * Self-merge is supported and doubles each counter up to UINT32_MAX.
         *
         * @param other Telemetry snapshot to add.
         */
        inline void merge(const Stats& other)
        {
            packetsReceived = saturating_add(
                packetsReceived,
                other.packetsReceived);

            cobsDecodeFailures = saturating_add(
                cobsDecodeFailures,
                other.cobsDecodeFailures);

            checksumMismatches = saturating_add(
                checksumMismatches,
                other.checksumMismatches);

            packetLengthMismatches = saturating_add(
                packetLengthMismatches,
                other.packetLengthMismatches);

            payloadSizeMismatches = saturating_add(
                payloadSizeMismatches,
                other.payloadSizeMismatches);

            packetEncodeFailures = saturating_add(
                packetEncodeFailures,
                other.packetEncodeFailures);

            cobsEncodeFailures = saturating_add(
                cobsEncodeFailures,
                other.cobsEncodeFailures);

            interByteTimeouts = saturating_add(
                interByteTimeouts,
                other.interByteTimeouts);

            ackTimeouts = saturating_add(
                ackTimeouts,
                other.ackTimeouts);

            receiveOverflows = saturating_add(
                receiveOverflows,
                other.receiveOverflows);

            unexpectedMessageTypes = saturating_add(
                unexpectedMessageTypes,
                other.unexpectedMessageTypes);

            unexpectedSequences = saturating_add(
                unexpectedSequences,
                other.unexpectedSequences);

            invalidAckResults = saturating_add(
                invalidAckResults,
                other.invalidAckResults);

            invalidHandshakes = saturating_add(
                invalidHandshakes,
                other.invalidHandshakes);
        }

        /**
         * @brief Write a human-readable telemetry summary.
         *
         * The return value follows snprintf() semantics. A nonnegative value is
         * the number of characters required, excluding the terminating NUL. When
         * that value is greater than or equal to @p capacity, the output was
         * truncated. A negative value indicates an invalid argument or formatting
         * failure.
         *
         * @param buffer Destination character buffer.
         * @param capacity Capacity of @p buffer in bytes, including the terminator.
         * @return Required character count excluding NUL, or -1 on failure.
         */
        inline int to_string(char* buffer, size_t capacity) const
        {
            if (!buffer || capacity == 0) {
                return -1;
            }

            return snprintf(
                buffer,
                capacity,
                "rx=%" PRIu32
                " cobs_dec=%" PRIu32
                " checksum=%" PRIu32
                " packet_len=%" PRIu32
                " payload_size=%" PRIu32
                " packet_enc=%" PRIu32
                " cobs_enc=%" PRIu32
                " inter_byte_tmo=%" PRIu32
                " ack_tmo=%" PRIu32
                " overflow=%" PRIu32
                " msg_type=%" PRIu32
                " sequence=%" PRIu32
                " ack_result=%" PRIu32
                " handshake=%" PRIu32,
                packetsReceived,
                cobsDecodeFailures,
                checksumMismatches,
                packetLengthMismatches,
                payloadSizeMismatches,
                packetEncodeFailures,
                cobsEncodeFailures,
                interByteTimeouts,
                ackTimeouts,
                receiveOverflows,
                unexpectedMessageTypes,
                unexpectedSequences,
                invalidAckResults,
                invalidHandshakes);
        }

    private:
        /** @brief Increment a counter without allowing unsigned wraparound. */
        static inline void saturating_increment(CountType& counter)
        {
            if (counter != UINT32_MAX) {
                ++counter;
            }
        }

        /** @brief Add two counters without allowing unsigned wraparound. */
        static inline CountType saturating_add(
            CountType left,
            CountType right)
        {
            return right > UINT32_MAX - left
                ? UINT32_MAX
                : left + right;
        }

        /** @brief Resolve a mutable counter for a declared telemetry event. */
        inline CountType* counter_for(TelemetryEvent event)
        {
            switch (event) {
            case TelemetryEvent::PacketReceived:
                return &packetsReceived;

            case TelemetryEvent::CobsDecodeFailed:
                return &cobsDecodeFailures;

            case TelemetryEvent::ChecksumMismatch:
                return &checksumMismatches;

            case TelemetryEvent::PacketLengthMismatch:
                return &packetLengthMismatches;

            case TelemetryEvent::PayloadSizeMismatch:
                return &payloadSizeMismatches;

            case TelemetryEvent::PacketEncodeFailed:
                return &packetEncodeFailures;

            case TelemetryEvent::CobsEncodeFailed:
                return &cobsEncodeFailures;

            case TelemetryEvent::InterByteTimeout:
                return &interByteTimeouts;

            case TelemetryEvent::AckTimeout:
                return &ackTimeouts;

            case TelemetryEvent::ReceiveOverflow:
                return &receiveOverflows;

            case TelemetryEvent::UnexpectedMessageType:
                return &unexpectedMessageTypes;

            case TelemetryEvent::UnexpectedSequence:
                return &unexpectedSequences;

            case TelemetryEvent::InvalidAckResult:
                return &invalidAckResults;

            case TelemetryEvent::InvalidHandshake:
                return &invalidHandshakes;

            default:
                return nullptr;
            }
        }

        /** @brief Resolve a read-only counter for a declared telemetry event. */
        inline const CountType* counter_for(TelemetryEvent event) const
        {
            switch (event) {
            case TelemetryEvent::PacketReceived:
                return &packetsReceived;

            case TelemetryEvent::CobsDecodeFailed:
                return &cobsDecodeFailures;

            case TelemetryEvent::ChecksumMismatch:
                return &checksumMismatches;

            case TelemetryEvent::PacketLengthMismatch:
                return &packetLengthMismatches;

            case TelemetryEvent::PayloadSizeMismatch:
                return &payloadSizeMismatches;

            case TelemetryEvent::PacketEncodeFailed:
                return &packetEncodeFailures;

            case TelemetryEvent::CobsEncodeFailed:
                return &cobsEncodeFailures;

            case TelemetryEvent::InterByteTimeout:
                return &interByteTimeouts;

            case TelemetryEvent::AckTimeout:
                return &ackTimeouts;

            case TelemetryEvent::ReceiveOverflow:
                return &receiveOverflows;

            case TelemetryEvent::UnexpectedMessageType:
                return &unexpectedMessageTypes;

            case TelemetryEvent::UnexpectedSequence:
                return &unexpectedSequences;

            case TelemetryEvent::InvalidAckResult:
                return &invalidAckResults;

            case TelemetryEvent::InvalidHandshake:
                return &invalidHandshakes;

            default:
                return nullptr;
            }
        }
    };

} // namespace tinylink

#endif // TINYLINK_STATS_H
