#ifndef TINYLINK_TEST_STM32_HAL_STUB_H
#define TINYLINK_TEST_STM32_HAL_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef int HAL_StatusTypeDef;
typedef uint32_t HAL_TickTypeDef;

struct UART_HandleTypeDef {
    void* Instance;
};

static const HAL_StatusTypeDef HAL_OK = 0;
static const HAL_StatusTypeDef HAL_ERROR = 1;
static const uint32_t UART_FLAG_RXNE = 0x20u;
static const uint32_t RESET = 0;
static const uint32_t SET = 1;

static bool tinylinkHalReceiveReady = false;
static HAL_StatusTypeDef tinylinkHalReceiveStatus = HAL_OK;
static uint8_t tinylinkHalReceiveValue = 0;
static uint32_t tinylinkHalReceiveTimeout = 0;
static size_t tinylinkHalReceiveCalls = 0;
static HAL_StatusTypeDef tinylinkHalTransmitStatus = HAL_OK;
static uint32_t tinylinkHalTransmitTimeout = 0;
static uint16_t tinylinkHalTransmitLength = 0;
static size_t tinylinkHalTransmitCalls = 0;
static uint8_t tinylinkHalTransmittedBytes[64] = {};
static uint32_t tinylinkHalTick = 0;

inline uint32_t tinylink_hal_get_flag(
    UART_HandleTypeDef*,
    uint32_t)
{
    return tinylinkHalReceiveReady ? SET : RESET;
}

#define __HAL_UART_GET_FLAG(handle, flag) \
    tinylink_hal_get_flag((handle), (flag))

inline HAL_StatusTypeDef HAL_UART_Receive(
    UART_HandleTypeDef*,
    uint8_t* value,
    uint16_t length,
    uint32_t timeout)
{
    ++tinylinkHalReceiveCalls;
    tinylinkHalReceiveTimeout = timeout;
    if (tinylinkHalReceiveStatus == HAL_OK && length > 0) {
        *value = tinylinkHalReceiveValue;
    }
    return tinylinkHalReceiveStatus;
}

inline HAL_StatusTypeDef HAL_UART_Transmit(
    UART_HandleTypeDef*,
    uint8_t* data,
    uint16_t length,
    uint32_t timeout)
{
    ++tinylinkHalTransmitCalls;
    tinylinkHalTransmitTimeout = timeout;
    tinylinkHalTransmitLength = length;
    if (data && length <= sizeof(tinylinkHalTransmittedBytes)) {
        for (size_t i = 0; i < length; ++i) {
            tinylinkHalTransmittedBytes[i] = data[i];
        }
    }
    return tinylinkHalTransmitStatus;
}

inline uint32_t HAL_GetTick()
{
    return tinylinkHalTick;
}

#endif // TINYLINK_TEST_STM32_HAL_STUB_H
