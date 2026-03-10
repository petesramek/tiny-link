/**
 * @file TinyESPIDFAdapter.h
 * @brief Native ESP-IDF (ESP32/ESP8266) serial adapter for TinyLink.
 *
 * This adapter uses the ESP-IDF UART driver directly and exposes the
 * TinyLink adapter interface.
 */

#ifndef TINY_ESP_IDF_ADAPTER_H
#define TINY_ESP_IDF_ADAPTER_H

#ifdef ESP_PLATFORM

#include "driver/uart.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stddef.h>

namespace tinylink {

    /**
     * @class TinyESPIDFAdapter
     * @brief ESP-IDF UART implementation for tinylink::TinyLink.
     *
     * Adapter contract (required by TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - void          write(uint8_t);
     *   - void          write(const uint8_t*, size_t);
     *   - unsigned long millis();
     */
    class TinyESPIDFAdapter {
    private:
        uart_port_t _uart_num;   /**< UART port number (UART_NUM_0, UART_NUM_1, etc.) */
        bool        _is_installed; /**< True if UART driver is installed and ready */

    public:
        /**
         * @brief Construct a new TinyESPIDFAdapter.
         * @param uart_num Initialized UART port identifier.
         */
        explicit TinyESPIDFAdapter(uart_port_t uart_num)
            : _uart_num(uart_num),
            _is_installed(uart_is_driver_installed(uart_num)) {
        }

        /**
         * @brief Returns true if the UART driver is installed and ready.
         */
        inline bool isOpen() {
            return _is_installed;
        }

        /**
         * @brief Returns number of bytes currently buffered in the UART RX buffer.
         */
        inline int available() {
            if (!_is_installed) return 0;
            size_t buffered_size = 0;
            if (uart_get_buffered_data_len(_uart_num, &buffered_size) != ESP_OK)
                return 0;
            return static_cast<int>(buffered_size);
        }

        /**
         * @brief Reads a single byte. Non-blocking.
         * @return Byte in range [0,255], or -1 if no data is available.
         */
        inline int read() {
            if (!_is_installed) return -1;

            uint8_t data = 0;
            // Non-blocking read (ticks_to_wait = 0)
            int length = uart_read_bytes(_uart_num, &data, 1, 0);
            return (length > 0) ? static_cast<int>(data) : -1;
        }

        /**
         * @brief Writes a single byte to the UART TX buffer.
         */
        inline void write(uint8_t c) {
            if (!_is_installed) return;
            uart_write_bytes(_uart_num, reinterpret_cast<const char*>(&c), 1);
        }

        /**
         * @brief Writes a buffer of bytes to the UART TX buffer.
         */
        inline void write(const uint8_t* b, size_t l) {
            if (!_is_installed || !b || l == 0) return;
            uart_write_bytes(_uart_num, reinterpret_cast<const char*>(b), l);
        }

        /**
         * @brief Returns system uptime in milliseconds (from esp_timer, µs -> ms).
         */
        inline unsigned long millis() {
            return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
        }
    };

} // namespace tinylink

#endif // ESP_PLATFORM
#endif // TINY_ESP_IDF_ADAPTER_H
