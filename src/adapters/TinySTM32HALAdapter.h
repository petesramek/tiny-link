/**
 * @file TinySTM32HALAdapter.h
 * @brief STM32 Hardware Abstraction Layer (HAL) adapter for TinyLink.
 */

#ifndef TINY_STM32_HAL_ADAPTER_H
#define TINY_STM32_HAL_ADAPTER_H

// The user must ensure the correct family header (e.g., stm32f4xx_hal.h) 
// is included in their build environment or global includes.
#if defined(HAL_UART_MODULE_ENABLED) || defined(STM32)
#include <stdint.h>
#include <stddef.h>

namespace tinylink {

    /**
     * @class TinySTM32HALAdapter
     * @brief Abstraction for STM32 HAL UART handles.
     */
    class TinySTM32HALAdapter {
    private:
        UART_HandleTypeDef* _huart; /**< Pointer to an initialized HAL UART handle */

    public:
        /**
         * @brief Construct a new STM32 HAL Adapter.
         * @param huart Pointer to the HAL UART handle (e.g., &huart1).
         */
        TinySTM32HALAdapter(UART_HandleTypeDef* huart) : _huart(huart) {}

        /** @brief Returns true if the UART handle and instance are valid. */
        inline bool isOpen() { 
            return (_huart != nullptr && _huart->Instance != nullptr); 
        }

        /** @brief Checks the RXNE (Receive Not Empty) flag. */
        inline int available() {
            if (!isOpen()) return 0;
            return (__HAL_UART_GET_FLAG(_huart, UART_FLAG_RXNE) != RESET) ? 1 : 0;
        }

        /** @brief Reads a single byte using a short-timeout polling receive. */
        inline int read() {
            if (!isOpen()) return -1;
            uint8_t data;
            if (HAL_UART_Receive(_huart, &data, 1, 1) == HAL_OK) {
                return (int)data;
            }
            return -1;
        }

        /** @brief Transmits a single byte via HAL polling. */
        inline void write(uint8_t c) {
            if (!isOpen()) return;
            HAL_UART_Transmit(_huart, &c, 1, 10);
        }

        /** @brief Transmits a buffer of bytes via HAL polling. */
        inline void write(const uint8_t* b, size_t l) {
            if (!isOpen()) return;
            HAL_UART_Transmit(_huart, (uint8_t*)b, (uint16_t)l, 100);
        }

        /** @brief Returns the current system tick count in milliseconds. */
        inline unsigned long millis() {
            return HAL_GetTick();
        }
    };
}

#endif // HAL_UART_MODULE_ENABLED
#endif // TINY_STM32_HAL_ADAPTER_H
