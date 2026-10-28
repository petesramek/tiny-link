/**
 * @file protocol/State.h
 * @brief Independent connection, receive, and transmit states for TinyLink.
 *
 * TinyLink tracks connection lifecycle, receive-parser progress, and transmit
 * acknowledgement independently. This allows normal simultaneous conditions,
 * such as receiving a frame while awaiting acknowledgement of an earlier
 * transmission.
 */
#ifndef TINYLINK_STATE_H
#define TINYLINK_STATE_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Public lifecycle state of a TinyLink connection.
     *
     * Values are explicit because this state may be exposed through the public
     * API. Receive-parser progress and transmit readiness do not affect this
     * lifecycle state.
     */
    enum class ConnectionState : uint8_t {
        Inactive = 0,     /**< Not started, reset, or awaiting synchronization after recovery. */
        Connecting = 1,   /**< Handshake negotiation is in progress. */
        Connected = 2,    /**< A compatible peer connection is established. */
        Incompatible = 3, /**< The peer uses an unsupported protocol version. */
        Failed = 4        /**< The adapter is unavailable or connection establishment failed. */
    };

    static_assert(sizeof(ConnectionState) == 1,
        "ConnectionState must have an 8-bit representation");

    static_assert(static_cast<uint8_t>(ConnectionState::Inactive) == 0,
        "ConnectionState::Inactive value changed");

    static_assert(static_cast<uint8_t>(ConnectionState::Connecting) == 1,
        "ConnectionState::Connecting value changed");

    static_assert(static_cast<uint8_t>(ConnectionState::Connected) == 2,
        "ConnectionState::Connected value changed");

    static_assert(static_cast<uint8_t>(ConnectionState::Incompatible) == 3,
        "ConnectionState::Incompatible value changed");

    static_assert(static_cast<uint8_t>(ConnectionState::Failed) == 4,
        "ConnectionState::Failed value changed");

    /**
     * @brief Internal progress of the receive-frame parser.
     *
     * ReceiveState is independent from connection and transmit state. In
     * particular, DiscardingFrame ignores every nonzero byte after an overflow
     * until the next zero frame delimiter is received.
     */
    enum class ReceiveState : uint8_t {
        WaitingForFrame, /**< No encoded frame bytes are currently buffered. */
        ReceivingFrame,  /**< COBS-encoded frame bytes are being accumulated. */
        DiscardingFrame  /**< Oversized frame bytes are ignored until delimiter. */
    };

    /**
     * @brief Internal readiness of the stop-and-wait transmit path.
     *
     * A connected link may be AwaitingAck while concurrently receiving another
     * frame. This state therefore does not represent connection lifecycle.
     */
    enum class TransmitState : uint8_t {
        Ready,      /**< No acknowledged transmission is currently pending. */
        AwaitingAck /**< One transmitted application frame awaits its ACK. */
    };

    /**
     * @brief Determine whether a connection state represents an established link.
     *
     * @param state Connection lifecycle state to inspect.
     * @return true only when @p state is ConnectionState::Connected.
     */
    inline bool is_connected(ConnectionState state)
    {
        return state == ConnectionState::Connected;
    }

    /**
     * @brief Determine whether the transmit path has a pending acknowledgement.
     *
     * @param state Transmit state to inspect.
     * @return true only when @p state is TransmitState::AwaitingAck.
     */
    inline bool is_awaiting_ack(TransmitState state)
    {
        return state == TransmitState::AwaitingAck;
    }

    /**
     * @brief Determine whether a connected link may start another acknowledged send.
     *
     * @param connectionState Current connection lifecycle state.
     * @param transmitState Current transmit readiness state.
     * @return true when connected and no acknowledgement is pending.
     */
    inline bool can_send(
        ConnectionState connectionState,
        TransmitState transmitState)
    {
        return is_connected(connectionState) &&
            transmitState == TransmitState::Ready;
    }

    /**
     * @brief Convert a connection state to a human-readable diagnostic string.
     *
     * @param state Connection state to describe.
     * @return Static diagnostic string, or "Invalid" for an undeclared value.
     */
    inline const char* connection_state_to_string(ConnectionState state)
    {
        switch (state) {
        case ConnectionState::Inactive:
            return "Inactive";

        case ConnectionState::Connecting:
            return "Connecting";

        case ConnectionState::Connected:
            return "Connected";

        case ConnectionState::Incompatible:
            return "Incompatible";

        case ConnectionState::Failed:
            return "Failed";

        default:
            return "Invalid";
        }
    }

} // namespace tinylink

#endif // TINYLINK_STATE_H
