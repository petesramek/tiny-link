/**
 * @file TinyTestAdapter.h
 * @brief Virtual adapter for unit testing and loopback simulation.
 *
 * This adapter is RAM-based and intended for host-side unit tests of
 * tinylink::TinyLink. It is not meant for production hardware.
 */

#ifndef TINY_TEST_ADAPTER_H
#define TINY_TEST_ADAPTER_H

#include <vector>
#include <stdint.h>
#include <stddef.h>
#include "TinyWriteResult.h"

namespace tinylink {

    /**
     * @class TinyTestAdapter
     * @brief A RAM-based serial simulator for verifying protocol logic on a PC.
     *
     * Adapter contract (compatible with TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - TinyWriteResult write(uint8_t);            // default: sink
     *   - TinyWriteResult write(const uint8_t*, size_t); // default: sink
     *   - unsigned long millis();
     *
     * Additionally provides:
     *   - setMillis() / advanceMillis() for manual time control
     *   - inject() / getRawBuffer() for RX injection & inspection
     */
    class TinyTestAdapter {
    protected:
        std::vector<uint8_t> _buffer;      /**< In-memory RX/capture storage. */
        unsigned long        _fakeMillis;  /**< Manually controlled test clock. */

    public:
        /**
         * @brief Construct a new TinyTestAdapter with millis = 0.
         */
        TinyTestAdapter()
            : _buffer(),
            _fakeMillis(0) {
        }

        /**
         * @brief Virtual destructor to support inherited loopback classes.
         */
        virtual ~TinyTestAdapter() {}

        /**
         * @brief Manually sets the internal clock value.
         * @note Time does not advance unless a test calls this or advanceMillis().
         */
        void setMillis(unsigned long m) { _fakeMillis = m; }

        /**
         * @brief Advances the internal clock by a specific duration.
         */
        void advanceMillis(unsigned long m) { _fakeMillis += m; }

        /**
         * @brief Injects raw bytes into the simulated RX buffer.
         * @note Null input and zero length are ignored; no physical I/O occurs.
         */
        void inject(const uint8_t* data, size_t len) {
            if (!data || len == 0) return;
            _buffer.insert(_buffer.end(), data, data + len);
        }

        /**
         * @brief Provides direct access to the internal buffer for error injection.
         */
        std::vector<uint8_t>& getRawBuffer() { return _buffer; }

        /**
         * @brief Simulated status: always returns true by default.
         * @note Real availability/failure behavior must be supplied by a subclass.
         */
        virtual bool isOpen() { return true; }

        /**
         * @brief Returns the number of bytes currently in the simulated RX buffer.
         */
        virtual int available() { return static_cast<int>(_buffer.size()); }

        /**
         * @brief Reads a byte from the front of the simulated buffer.
         * @return Byte in range [0,255], or -1 if buffer is empty.
         */
        virtual int read() {
            if (_buffer.empty()) return -1;
            uint8_t b = _buffer.front();
            _buffer.erase(_buffer.begin());
            return static_cast<int>(b);
        }

        /**
         * @brief Virtual write method (override in loopback tests).
         * Default implementation reports a successful sink; it does not retain
         * or transmit the byte and does not model hardware timing.
         */
        virtual TinyWriteResult write(uint8_t c) {
            return write(&c, 1);
        }

        /**
         * @brief Virtual write method (override in loopback tests).
         * Default implementation reports the full non-null buffer accepted
         * while discarding it; a null non-empty buffer reports failure.
         */
        virtual TinyWriteResult write(const uint8_t* b, size_t l) {
            if (l == 0) return tiny_write_result(0, 0, true);
            return tiny_write_result(l, b ? l : 0, b != nullptr);
        }

        /**
         * @brief Returns the manual clock value.
         */
        virtual unsigned long millis() { return _fakeMillis; }
    };

} // namespace tinylink

#endif // TINY_TEST_ADAPTER_H
