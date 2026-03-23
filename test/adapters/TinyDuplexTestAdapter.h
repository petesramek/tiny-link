/**
 * @file adapters/TinyDuplexTestAdapter.h
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

#include "adapters/TinyTestAdapter.h"

namespace tinylink {

    class TinyDuplexTestAdapter : public TinyTestAdapter {
        TinyDuplexTestAdapter* _peer;

    public:
        TinyDuplexTestAdapter() : _peer(nullptr) {}

        /** @brief Point this adapter's TX at the peer's RX buffer. */
        void connect(TinyDuplexTestAdapter& peer) { _peer = &peer; }

        void write(uint8_t c) override {
            if (_peer) _peer->inject(&c, 1);
        }

        void write(const uint8_t* buf, size_t len) override {
            if (_peer) _peer->inject(buf, len);
        }
    };

} // namespace tinylink

#endif // TINY_DUPLEX_TEST_ADAPTER_H
