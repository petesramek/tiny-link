/**
 * @file TinyDuplexTestAdapter.h
 * @brief Cross-connected duplex adapter for two-instance protocol tests.
 *
 * Connects two TinyLink instances so that writes from one go directly into
 * the read buffer of the other, simulating a bidirectional serial wire.
 *
 * Usage:
 *   TinyDuplexTestAdapter adapterA, adapterB;
 *   adapterA.connect(adapterB);   // A's writes → B's RX buffer
 *   adapterB.connect(adapterA);   // B's writes → A's RX buffer
 *
 *   TinyLink<Payload, TinyDuplexTestAdapter> linkA(adapterA);
 *   TinyLink<Payload, TinyDuplexTestAdapter> linkB(adapterB);
 */

#ifndef TINY_DUPLEX_TEST_ADAPTER_H
#define TINY_DUPLEX_TEST_ADAPTER_H

#include "TinyTestAdapter.h"

namespace tinylink {

    /**
     * @class TinyDuplexTestAdapter
     * @brief In-memory transport connecting one TinyLink instance to a peer.
     *
     * Writes are appended directly to the peer's RX buffer; this models byte
     * delivery, not real serial timing, buffering limits, or flow control.
     * setOpen() affects isOpen() only; writes remain atomic if a peer is set.
     */
    class TinyDuplexTestAdapter : public TinyTestAdapter {
        TinyDuplexTestAdapter* _peer; /**< Non-owning destination for transmitted bytes. */
        bool _open;                   /**< Test-controlled transport availability. */

    public:
        TinyDuplexTestAdapter() : _peer(nullptr), _open(true) {}

        /**
         * @brief Point this adapter's TX at the peer's RX buffer.
         * @note The peer must outlive this adapter's use; connect both ends for
         *       bidirectional traffic.
         */
        void connect(TinyDuplexTestAdapter& peer) { _peer = &peer; }

        /** @brief Set the simulated transport state returned by isOpen(). */
        void setOpen(bool open) { _open = open; }

        bool isOpen() override { return _open; }

        TinyWriteResult write(uint8_t c) override {
            return write(&c, 1);
        }

        TinyWriteResult write(const uint8_t* buf, size_t len) override {
            if (len == 0) return tiny_write_result(0, 0, true);
            // Unlike a real adapter this stub delivers the full write atomically.
            if (!_peer || !buf) return tiny_write_result(len, 0, false);
            _peer->inject(buf, len);
            return tiny_write_result(len, len, true);
        }
    };

} // namespace tinylink

#endif // TINY_DUPLEX_TEST_ADAPTER_H
