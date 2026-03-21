/**
 * @file TinyProtocol.h
 * @brief Core constants, enums, and telemetry structures for TinyLink.
 */

#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include <stdint.h>

namespace tinylink {

    // ------------------------------------------------------------
    // Message Types (Wire Format)
    // ------------------------------------------------------------

    /**
     * @brief Message type identifiers used in TinyLink frames.
     *
     * These are 1‑byte ASCII codes placed in pBuf[0].
     * Using enum class ensures type safety while allowing easy casting.
     */
    enum class MessageType : uint8_t {
        Data    = 'D',  /**< Standard data payload */
        Command = 'C',  /**< Instruction to the other end (replaces Req) */
        Ack     = 'A',  /**< Combined ACK/NACK — carries a TinyResult code */
        Debug   = 'g',  /**< Diagnostics / crash report forwarding */
        Status  = 'S',  /**< Boot-time state exchange / handshake */
    };

    /** @brief Convenience aliases for the MessageType enum values. */
    constexpr uint8_t TYPE_DATA    = static_cast<uint8_t>(MessageType::Data);
    constexpr uint8_t TYPE_COMMAND = static_cast<uint8_t>(MessageType::Command);
    constexpr uint8_t TYPE_ACK     = static_cast<uint8_t>(MessageType::Ack);
    constexpr uint8_t TYPE_DEBUG   = static_cast<uint8_t>(MessageType::Debug);
    constexpr uint8_t TYPE_STATUS  = static_cast<uint8_t>(MessageType::Status);

    // ------------------------------------------------------------
    // High-Level Status Codes (legacy — use TinyResult for new code)
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
    // Granular Result Codes
    // ------------------------------------------------------------

    /**
     * @brief Granular result codes carried in TinyAck.result.
     */
    enum class TinyResult : uint8_t {
        OK             = 0x00,  /**< Processed successfully */
        ERR_CRC        = 0x01,  /**< Checksum or framing failure */
        ERR_TIMEOUT    = 0x02,  /**< Inter-byte timeout */
        ERR_OVERFLOW   = 0x03,  /**< Buffer overflow */
        ERR_BUSY       = 0x04,  /**< Engine not ready (_hasNew not cleared) */
        ERR_PROCESSING = 0x05,  /**< Received OK but application-level failure */
        ERR_UNKNOWN    = 0xFF,  /**< Catch-all */
    };

    // ------------------------------------------------------------
    // Protocol Payload Structs
    // ------------------------------------------------------------

    /**
     * @brief Payload for TYPE_ACK messages.
     */
    struct TinyAck {
        uint8_t    seq;     /**< Sequence number being acknowledged */
        TinyResult result;  /**< Granular result code */
    } __attribute__((packed)); // 2 bytes

    /**
     * @brief Payload for TYPE_STATUS messages (handshake + state exchange).
     */
    struct TinyStatusPayload {
        uint8_t    state;    /**< Current TinyState of the sender */
        uint8_t    lastSeq;  /**< Last sequence number successfully processed */
        TinyResult lastErr;  /**< Last error code (0x00 = OK) */
        uint8_t    flags;    /**< User-defined bitmask: bit0=WiFi, bit1=MQTT, etc. */
    } __attribute__((packed)); // 4 bytes

    // ------------------------------------------------------------
    // Protocol State Machine
    // ------------------------------------------------------------

    /**
     * @brief Full state machine for the TinyLink connection and RX pipeline.
     */
    enum class TinyState : uint8_t {
        CONNECTING,     /**< Boot state — sends TYPE_STATUS, waits for other end's STATUS */
        HANDSHAKING,    /**< STATUS received and ACK sent, waiting for ACK to our STATUS */
        WAIT_FOR_SYNC,  /**< Connected — normal operation, waiting for 0x00 frame delimiter */
        IN_FRAME,       /**< Accumulating encoded bytes until next 0x00 */
        FRAME_COMPLETE, /**< Valid frame received, ready to process */
        AWAITING_ACK,   /**< Sent a packet, waiting for Ack response */
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
