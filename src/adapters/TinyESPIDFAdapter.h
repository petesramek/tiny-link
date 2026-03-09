#ifndef TINY_ESP_IDF_ADAPTER_H
#define TINY_ESP_IDF_ADAPTER_H

#include "driver/uart.h"
#include "esp_timer.h"

class TinyESPIDFAdapter {
private:
    uart_port_t _uart_num;

public:
    TinyESPIDFAdapter(uart_port_t uart_num) : _uart_num(uart_num) {}

    inline int available() {
        size_t buffered_size;
        uart_get_buffered_data_len(_uart_num, &buffered_size);
        return (int)buffered_size;
    }

    inline int read() {
        uint8_t data;
        int length = uart_read_bytes(_uart_num, &data, 1, 0);
        return (length > 0) ? data : -1;
    }

    inline void write(uint8_t c) {
        uart_write_bytes(_uart_num, (const char*)&c, 1);
    }

    inline void write(const uint8_t* b, size_t l) {
        uart_write_bytes(_uart_num, (const char*)b, l);
    }

    inline unsigned long millis() {
        return (unsigned long)(esp_timer_get_time() / 1000);
    }
};
#endif
