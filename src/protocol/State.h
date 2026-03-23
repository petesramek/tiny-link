/**
 * @file State.h
 * @brief TinyState enum — internal state machine stages for TinyLink.
 */

#ifndef TINYLINK_STATE_H
#define TINYLINK_STATE_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Internal state stages for the TinyLink connection state machine.
     */
    enum class TinyState : uint8_t {
        WAIT_FOR_SYNC,  /**< Default state — ready to receive frames (no handshake in progress) */
        CONNECTING,     /**< startHandshake() was called — Handshake frame sent, waiting for peer's ACK */
        HANDSHAKING,    /**< Peer's Handshake received and replied to — handshake sequence complete */
    };

} // namespace tinylink

#endif // TINYLINK_STATE_H
