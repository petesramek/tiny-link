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
        bool _open;

    public:
        TinyDuplexTestAdapter() : _peer(nullptr), _open(true) {}

        /** @brief Point this adapter's TX at the peer's RX buffer. */
        void connect(TinyDuplexTestAdapter& peer) { _peer = &peer; }

        /** @brief Simulate the transport becoming available or unavailable. */
        void setOpen(bool open) { _open = open; }

        bool isOpen() override { return _open; }

        TinyWriteResult write(uint8_t c) override {
            return write(&c, 1);
        }

        TinyWriteResult write(const uint8_t* buf, size_t len) override {
            if (len == 0) return tiny_write_result(0, 0, true);
            if (!_peer || !buf) return tiny_write_result(len, 0, false);
            _peer->inject(buf, len);
            return tiny_write_result(len, len, true);
        }
    };

} // namespace tinylink

#endif // TINY_DUPLEX_TEST_ADAPTER_H
