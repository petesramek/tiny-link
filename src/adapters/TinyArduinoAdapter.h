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
 * Stream::write() byte counts are exposed as TinyLink write results. Partial
 * writes are not retried by this adapter.
 */
#ifndef TINYLINK_ADAPTERS_TINYARDUINOADAPTER_H
#define TINYLINK_ADAPTERS_TINYARDUINOADAPTER_H

#ifdef ARDUINO

#include <Arduino.h>

#include <stddef.h>
#include <stdint.h>
#include "../TinyWriteResult.h"

#include "adapters/WriteResult.h"

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
     * @return Complete, partial, or failed result from Stream::write().
     *
     * @param value Byte to write.
     */
    inline WriteResult write(uint8_t value)
    {
        return WriteResult::fromCount(1, _port.write(value));
    }

    /**
     * @brief Write a byte sequence to the underlying Stream.
     *
     * A null data pointer is valid only when @p length is zero. Partial writes
     * are returned without retrying.
     *
     * @param data Bytes to write, or nullptr when @p length is zero.
     * @param length Number of bytes to write.
     * @return Outcome and accepted byte count.
     */
    inline WriteResult write(
        const uint8_t* data,
        size_t length)
    {
        if (length == 0) {
            return WriteResult::fromCount(0, 0);
        }

        if (!data) {
            return WriteResult::failure();
        }

        return WriteResult::fromCount(length, _port.write(data, length));
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
