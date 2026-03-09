/**
 * @file TinyESPIDFAdapter.h
 * @brief Native ESP-IDF (ESP32/ESP8266) serial adapter for TinyLink.
 */

#ifndef TINY_ESP_IDF_ADAPTER_H
#define TINY_ESP_IDF_ADAPTER_H

#ifdef ESP_PLATFORM
#include "driver/uart.h"
#include "esp_timer.h"
#include <stdint.h>

namespace tinylink {

    /**
     * @class TinyESPIDFAdapter
     * @brief Direct ESP-IDF implementation for Serial communication using the UART driver.
     */
    class TinyESPIDFAdapter {
    private:
        uart_port_t _uart_num; /**< The UART port number (e.g., UART_NUM_0, UART_NUM_1) */
        bool _is_installed;    /**< Internal check for UART driver status */

    public:
        /**
         * @brief Construct a new ESP-IDF Serial Adapter.
         * @param uart_num The initialized UART port identifier.
         */
        TinyESPIDFAdapter(uart_port_t uart_num) : _uart_num(uart_num) {
            _is_installed = uart_is_driver_installed(_uart_num);
        }

        /** @brief Returns true if the UART driver is installed and ready. */
        inline bool isOpen() { return _is_installed; }

        /** @brief Returns number of bytes currently in the UART RX buffer. */
        inline int available() {
            if (!_is_installed) return 0;
            size_t buffered_size;
            uart_get_buffered_data_len(_uart_num, &buffered_size);
            return (int)buffered_size;
        }

        /** @brief Reads a single byte. Returns -1 if no data is available. */
        inline int read() {
            if (!_is_installed) return -1;
            uint8_t data;
            // Non-blocking read (ticks_to_wait = 0)
            int length = uart_read_bytes(_uart_num, &data, 1, 0);
            return (length > 0) ? (int)data : -1;
        }

        /** @brief Writes a single byte to the UART TX buffer. */
        inline void write(uint8_t c) {
            if (!_is_installed) return;
            uart_write_bytes(_uart_num, (const char*)&c, 1);
        }

        /** @brief Writes a buffer of bytes to the UART TX buffer. */
        inline void write(const uint8_t* b, size_t l) {
            if (!_is_installed) return;
            uart_write_bytes(_uart_num, (const char*)b, l);
        }

        /** @brief Returns system uptime in milliseconds (converted from microseconds). */
        inline unsigned long millis() {
            return (unsigned long)(esp_timer_get_time() / 1000);
        }
    };
}

#endif // ESP_PLATFORM
#endif // TINY_ESP_IDF_ADAPTER_H
