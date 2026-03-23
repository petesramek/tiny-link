/**
 * @file State.h
 * @brief TinyState enum — internal state machine stages for TinyLink.
 */

#ifndef TINYLINK_STATE_H
#define TINYLINK_STATE_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief State machine stages for the TinyLink connection and receive pipeline.
     *
     * Transitions at a glance:
     *
     *   begin() called
     *       → CONNECTING    : HS(v=0) sent; waiting for peer's HS reply.
     *                         Periodically re-sends until a reply arrives.
     *
     *   CONNECTING + HS(v=0) or HS(v=1) received from peer
     *       → WAIT_FOR_SYNC : Fully connected; idle between frames.
     *
     *   WAIT_FOR_SYNC + first non-zero byte of an incoming frame
     *       → IN_FRAME      : Accumulating COBS-encoded bytes.
     *
     *   IN_FRAME + 0x00 delimiter (frame boundary)
     *       → FRAME_COMPLETE: Decoding and dispatching the completed frame (brief).
     *
     *   FRAME_COMPLETE → WAIT_FOR_SYNC (or AWAITING_ACK if an Ack is still pending)
     *
     *   sendData() in handshake mode
     *       → AWAITING_ACK  : Data frame sent; waiting for an explicit Ack reply.
     *                         Returns to WAIT_FOR_SYNC on receipt of Ack or timeout.
     */
    enum class TinyState : uint8_t {
        CONNECTING,     /**< begin() sent HS(v=0); waiting for peer's HS reply */
        WAIT_FOR_SYNC,  /**< Connected and idle; waiting for next 0x00 frame delimiter */
        IN_FRAME,       /**< Accumulating COBS-encoded bytes between 0x00 delimiters */
        FRAME_COMPLETE, /**< 0x00 delimiter received; decoding and dispatching frame */
        AWAITING_ACK    /**< Data frame sent (handshake mode); waiting for Ack reply */
    };

} // namespace tinylink

#endif // TINYLINK_STATE_H
