/**
 * @file TinyTestAdapter.h
 * @brief Virtual adapter for unit testing and loopback simulation.
 */

#ifndef TINY_TEST_ADAPTER_H
#define TINY_TEST_ADAPTER_H

#include <vector>
#include <stdint.h>
#include <stddef.h>

namespace tinylink {

    /**
     * @class TinyTestAdapter
     * @brief A RAM-based serial simulator for verifying protocol logic on a PC.
     */
    class TinyTestAdapter {
    protected: 
        std::vector<uint8_t> _buffer; /**< Internal byte storage for simulation */
        unsigned long _fakeMillis = 0; /**< Manual clock for timeout testing */

    public:
        /** @brief Virtual destructor to support inherited loopback classes. */
        virtual ~TinyTestAdapter() {} 

        /** @brief Manually sets the internal clock value. */
        void setMillis(unsigned long m) { _fakeMillis = m; }
        
        /** @brief Advances the internal clock by a specific duration. */
        void advanceMillis(unsigned long m) { _fakeMillis += m; }
        
        /** @brief Injects raw bytes into the simulated RX buffer. */
        void inject(const uint8_t* data, size_t len) {
            _buffer.insert(_buffer.end(), data, data + len);
        }

        /** @brief Provides direct access to the internal buffer for error injection. */
        std::vector<uint8_t>& getRawBuffer() { return _buffer; }

        /** @brief Simulated status: always returns true. */
        virtual bool isOpen() { return true; }
        
        /** @brief Returns the number of bytes currently in the simulated RX buffer. */
        virtual int available() { return (int)_buffer.size(); }
        
        /** @brief Reads a byte from the front of the simulated buffer. */
        virtual int read() {
            if (_buffer.empty()) return -1;
            uint8_t b = _buffer.front();
            _buffer.erase(_buffer.begin());
            return (int)b;
        }

        /** @brief Virtual write method (override in loopback tests). */
        virtual void write(uint8_t c) { (void)c; }
        
        /** @brief Virtual write method (override in loopback tests). */
        virtual void write(const uint8_t* b, size_t l) { (void)b; (void)l; }
        
        /** @brief Returns the manual clock value. */
        virtual unsigned long millis() { return _fakeMillis; }
    };
}

#endif // TINY_TEST_ADAPTER_H
