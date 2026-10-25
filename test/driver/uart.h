#ifndef TINYLINK_TEST_ESP_IDF_UART_STUB_H
#define TINYLINK_TEST_ESP_IDF_UART_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef int uart_port_t;
typedef int esp_err_t;

static const uart_port_t UART_NUM_0 = 0;
static const esp_err_t ESP_OK = 0;
static const esp_err_t ESP_FAIL = -1;

static bool tinylinkEspDriverInstalled = true;
static esp_err_t tinylinkEspBufferStatus = ESP_OK;
static size_t tinylinkEspBufferedSize = 0;
static int tinylinkEspReadResult = 0;
static uint8_t tinylinkEspReadValue = 0;
static size_t tinylinkEspWriteCalls = 0;
static size_t tinylinkEspLastWriteLength = 0;
static uint8_t tinylinkEspLastWriteBytes[64] = {};

inline bool uart_is_driver_installed(uart_port_t)
{
    return tinylinkEspDriverInstalled;
}

inline esp_err_t uart_get_buffered_data_len(
    uart_port_t,
    size_t* bufferedSize)
{
    if (tinylinkEspBufferStatus != ESP_OK) {
        return tinylinkEspBufferStatus;
    }
    *bufferedSize = tinylinkEspBufferedSize;
    return ESP_OK;
}

inline int uart_read_bytes(
    uart_port_t,
    uint8_t* destination,
    size_t length,
    uint32_t)
{
    if (tinylinkEspReadResult == 1 && length > 0) {
        *destination = tinylinkEspReadValue;
    }
    return tinylinkEspReadResult;
}

inline int uart_write_bytes(
    uart_port_t,
    const char* data,
    size_t length)
{
    ++tinylinkEspWriteCalls;
    tinylinkEspLastWriteLength = length;
    if (data && length <= sizeof(tinylinkEspLastWriteBytes)) {
        for (size_t i = 0; i < length; ++i) {
            tinylinkEspLastWriteBytes[i] = static_cast<uint8_t>(data[i]);
        }
    }
    return static_cast<int>(length);
}

#endif // TINYLINK_TEST_ESP_IDF_UART_STUB_H
