#ifndef TINY_ESP_IDF_ADAPTER_H
#define TINY_ESP_IDF_ADAPTER_H

#include "driver/uart.h"
#include "esp_timer.h"
#include <stdint.h>

class TinyESPIDFAdapter {
private:
    uart_port_t _uart_num;
    bool _is_installed;

public:
    // User passes initialized UART port (e.g., UART_NUM_1)
    TinyESPIDFAdapter(uart_port_t uart_num) : _uart_num(uart_num) {
        // Check if the driver is installed for this port
        _is_installed = uart_is_driver_installed(_uart_num);
    }

    // --- TinyAdapter Interface ---
    inline bool isOpen() { return _is_installed; }

    inline int available() {
        if (!_is_installed) return 0;
        size_t buffered_size;
        uart_get_buffered_data_len(_uart_num, &buffered_size);
        return (int)buffered_size;
    }

    inline int read() {
        if (!_is_installed) return -1;
        uint8_t data;
        // Non-blocking read (tick_to_wait = 0)
        int length = uart_read_bytes(_uart_num, &data, 1, 0);
        return (length > 0) ? (int)data : -1;
    }

    inline void write(uint8_t c) {
        if (!_is_installed) return;
        uart_write_bytes(_uart_num, (const char*)&c, 1);
    }

    inline void write(const uint8_t* b, size_t l) {
        if (!_is_installed) return;
        uart_write_bytes(_uart_num, (const char*)b, l);
    }

    inline unsigned long millis() {
        // esp_timer_get_time returns microseconds since boot
        return (unsigned long)(esp_timer_get_time() / 1000);
    }
};

#endif
