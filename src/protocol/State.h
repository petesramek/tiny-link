/**
 * @file State.h
 * @brief TinyState enum — internal state machine stages for TinyLink.
 */

#ifndef TINYLINK_STATE_H
#define TINYLINK_STATE_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Internal state stages for the TinyLink state machine.
     */
    enum class TinyState : uint8_t {
        CONNECTING,      /**< Boot state — sends TYPE_STATUS, waits for peer's STATUS */
        HANDSHAKING,     /**< STATUS received and ACK sent, waiting for ACK to our STATUS */
        WAIT_FOR_SYNC,   /**< Connected — waiting for 0x00 frame delimiter */
        IN_FRAME,        /**< Accumulating encoded bytes until next 0x00 */
        FRAME_COMPLETE,  /**< Valid frame received, ready to process */
        AWAITING_ACK     /**< Sent a packet, waiting for Ack response */
    };

} // namespace tinylink

#endif // TINYLINK_STATE_H
