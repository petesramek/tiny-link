/**
 * @file adapters/TinyArduinoAdapter.h
 * @brief TinyLink transport adapter for an Arduino Stream.
 *
 * The adapter is a non-owning wrapper. The supplied Stream instance must
 * remain alive for the entire lifetime of the adapter and every TinyLink
 * instance that uses it.
 *
 * Arduino Stream does not provide a generic connection-state contract.
 * Consequently, isOpen() reports that the adapter is bound to its required
 * Stream instance; it does not verify physical transport connectivity.
 *
 * Write methods return TinyWriteResult so callers can inspect the byte count
 * reported by Stream::write().
 */
#ifndef TINYLINK_ADAPTERS_TINYARDUINOADAPTER_H
#define TINYLINK_ADAPTERS_TINYARDUINOADAPTER_H

#ifdef ARDUINO

#include <Arduino.h>

#include <stddef.h>
#include <stdint.h>
#include "../TinyWriteResult.h"

namespace tinylink {

/**
 * @brief Non-owning TinyLink adapter over an Arduino Stream.
 *
 * The wrapped Stream must outlive this adapter. This class does not open,
 * close, configure, or own the underlying transport.
 */
class TinyArduinoAdapter {
private:
    Stream& _port;

public:
    /**
     * @brief Construct an adapter over an existing Arduino Stream.
     *
     * @param port Stream used for all TinyLink reads and writes. The caller
     * must keep @p port alive for the lifetime of this adapter.
     */
    explicit TinyArduinoAdapter(Stream& port)
        : _port(port)
    {}

    /**
     * @brief Determine whether the adapter is bound to its required Stream.
     *
     * A generic Arduino Stream does not expose physical connection readiness.
     * Because construction requires a Stream reference, this method always
     * returns true for a valid adapter object.
     *
     * @return Always true.
     */
    inline bool isOpen() const
    {
        return true;
    }

    /**
     * @brief Return the number of bytes currently available to read.
     *
     * @return Value reported by Stream::available().
     */
    inline int available()
    {
        return _port.available();
    }

    /**
     * @brief Read one byte from the underlying Stream.
     *
     * @return Byte in the range 0 through 255, or -1 when no byte is available.
     */
    inline int read()
    {
        return _port.read();
    }

    /**
     * @brief Write one byte to the underlying Stream.
     *
     * The Stream::write() byte count is returned as a TinyWriteResult.
     *
     * @param value Byte to write.
     */
    inline TinyWriteResult write(uint8_t value)
    {
        const size_t written = _port.write(value);
        return tiny_write_result(1, written, written == 1);
    }

    /**
     * @brief Write a byte sequence to the underlying Stream.
     *
     * A zero-length request does not call Stream::write(). A null data pointer
     * is valid only when @p length is zero.
     *
     * @param data Bytes to write, or nullptr when @p length is zero.
     * @param length Number of bytes to write.
     */
    inline TinyWriteResult write(
        const uint8_t* data,
        size_t length)
    {
        if (length == 0) {
            return tiny_write_result(0, 0, true);
        }

        if (!data) {
            return tiny_write_result(length, 0, false);
        }

        const size_t written = _port.write(data, length);
        return tiny_write_result(length, written, written == length);
    }

    /**
     * @brief Return Arduino system uptime in milliseconds.
     *
     * @return Current value of the global Arduino millis() clock.
     */
    inline unsigned long millis() const
    {
        return ::millis();
    }
};

} // namespace tinylink

#endif // ARDUINO

#endif // TINYLINK_ADAPTERS_TINYARDUINOADAPTER_H
