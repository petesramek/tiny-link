/**
 * @file adapters/TinyESPIDFAdapter.h
 * @brief TinyLink transport adapter for an ESP-IDF UART driver.
 *
 * The adapter is non-owning. It does not install, configure, flush, or delete
 * the ESP-IDF UART driver. The application remains responsible for the UART
 * driver's configuration and lifetime.
 *
 * Return values from uart_write_bytes() are exposed as TinyLink write results.
 * Partial writes are not retried by this adapter.
 */
#ifndef TINYLINK_ADAPTERS_TINYESPIDFADAPTER_H
#define TINYLINK_ADAPTERS_TINYESPIDFADAPTER_H

#ifdef ESP_PLATFORM

#include "driver/uart.h"
#include "esp_timer.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "adapters/WriteResult.h"

namespace tinylink {

/**
 * @brief Non-owning TinyLink adapter over an ESP-IDF UART port.
 *
 * The application must install and configure the selected UART driver before
 * using the adapter. Driver readiness is queried dynamically so installation
 * or removal after adapter construction is reflected by isOpen().
 */
class TinyESPIDFAdapter {
private:
    uart_port_t _uartPort;

public:
    /**
     * @brief Construct an adapter for an ESP-IDF UART port.
     *
     * Construction does not install or configure the UART driver.
     *
     * @param uartPort UART port identifier such as UART_NUM_0 or UART_NUM_1.
     */
    explicit TinyESPIDFAdapter(uart_port_t uartPort)
        : _uartPort(uartPort)
    {}

    /**
     * @brief Determine whether the selected UART driver is installed.
     *
     * @return true when ESP-IDF reports an installed driver for the port.
     */
    inline bool isOpen() const
    {
        return uart_is_driver_installed(_uartPort);
    }

    /**
     * @brief Return the number of bytes buffered for non-blocking reading.
     *
     * A driver error is represented as zero available bytes. Values above
     * INT_MAX are saturated because the TinyLink adapter contract returns int.
     *
     * @return Buffered byte count, or zero when unavailable or on error.
     */
    inline int available() const
    {
        if (!isOpen()) {
            return 0;
        }

        size_t bufferedSize = 0;

        if (uart_get_buffered_data_len(
                _uartPort,
                &bufferedSize) != ESP_OK) {
            return 0;
        }

        if (bufferedSize > static_cast<size_t>(INT_MAX)) {
            return INT_MAX;
        }

        return static_cast<int>(bufferedSize);
    }

    /**
     * @brief Read one byte without waiting.
     *
     * ESP-IDF errors and an empty receive buffer are both represented as -1 by
     * the current TinyLink adapter contract.
     *
     * @return Byte in the range 0 through 255, or -1 when no byte was read.
     */
    inline int read()
    {
        if (!isOpen()) {
            return -1;
        }

        uint8_t value = 0;
        const int bytesRead = uart_read_bytes(
            _uartPort,
            &value,
            1,
            0);

        return bytesRead == 1
            ? static_cast<int>(value)
            : -1;
    }

    /**
     * @brief Queue one byte for transmission.
     *
     * @return Complete or failed result reported by ESP-IDF.
     *
     * @param value Byte to write.
     */
    inline WriteResult write(uint8_t value)
    {
        if (!isOpen()) {
            return WriteResult::failure();
        }

        const int bytesWritten = uart_write_bytes(
            _uartPort,
            reinterpret_cast<const char*>(&value),
            1);
        return bytesWritten < 0
            ? WriteResult::failure()
            : WriteResult::fromCount(1, static_cast<size_t>(bytesWritten));
    }

    /**
     * @brief Queue a byte sequence for transmission.
     *
     * Partial writes are returned without retrying. A null data pointer is
     * valid only when @p length is zero.
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

        if (!isOpen() || !data || length > static_cast<size_t>(INT_MAX)) {
            return WriteResult::failure();
        }

        const int bytesWritten = uart_write_bytes(
            _uartPort,
            reinterpret_cast<const char*>(data),
            length);
        return bytesWritten < 0
            ? WriteResult::failure()
            : WriteResult::fromCount(
                length,
                static_cast<size_t>(bytesWritten));
    }

    /**
     * @brief Return ESP-IDF monotonic uptime in milliseconds.
     *
     * The ESP-IDF microsecond timer is converted to the unsigned-long
     * millisecond clock expected by TinyLink. The value may wrap when unsigned
     * long is 32 bits; TinyLink uses unsigned elapsed-time subtraction.
     *
     * @return Monotonic uptime in milliseconds.
     */
    inline unsigned long millis() const
    {
        return static_cast<unsigned long>(
            static_cast<uint64_t>(esp_timer_get_time()) / 1000u);
    }
};

} // namespace tinylink

#endif // ESP_PLATFORM

#endif // TINYLINK_ADAPTERS_TINYESPIDFADAPTER_H
