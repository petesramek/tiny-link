/**
 * @file TinySTM32HALAdapter.h
 * @brief STM32 Hardware Abstraction Layer (HAL) adapter for TinyLink.
 *
 * This adapter wraps an STM32 HAL UART handle and exposes the TinyLink
 * adapter interface.
 */

#ifndef TINY_STM32_HAL_ADAPTER_H
#define TINY_STM32_HAL_ADAPTER_H

 // The user must ensure the correct family header (e.g., stm32f4xx_hal.h)
 // is included in their project before including this file.
#if defined(HAL_UART_MODULE_ENABLED) && defined(STM32)

#include <stdint.h>
#include <stddef.h>
#include "../TinyWriteResult.h"

namespace tinylink {

    /**
     * @class TinySTM32HALAdapter
     * @brief Abstraction for STM32 HAL UART handles.
     *
     * Adapter contract (required by TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - TinyWriteResult write(uint8_t);
     *   - TinyWriteResult write(const uint8_t*, size_t);
     *   - unsigned long millis();
     */
    class TinySTM32HALAdapter {
    private:
        UART_HandleTypeDef* _huart; /**< Pointer to an initialized HAL UART handle */

    public:
        /**
         * @brief Construct a new TinySTM32HALAdapter.
         * @param huart Pointer to the HAL UART handle (e.g., &huart1).
         */
        explicit TinySTM32HALAdapter(UART_HandleTypeDef* huart)
            : _huart(huart) {
        }

        /**
         * @brief Returns true if the UART handle and instance are valid.
         */
        inline bool isOpen() {
            return (_huart != nullptr && _huart->Instance != nullptr);
        }

        /**
         * @brief Checks the RXNE (Receive Not Empty) flag.
         * @return 1 if at least one byte is available, 0 otherwise.
         */
        inline int available() {
            if (!isOpen()) return 0;
            return (__HAL_UART_GET_FLAG(_huart, UART_FLAG_RXNE) != RESET) ? 1 : 0;
        }

        /**
         * @brief Reads a single byte using a short-timeout polling receive.
         * @return Byte in range [0,255], or -1 if no data is available or on error.
         */
        inline int read() {
            if (!isOpen()) return -1;

            uint8_t data = 0;
            if (HAL_UART_Receive(_huart, &data, 1, 1) == HAL_OK) {
                return static_cast<int>(data);
            }
            return -1;
        }

        /**
         * @brief Transmits a single byte via HAL polling.
         */
        inline TinyWriteResult write(uint8_t c) {
            if (!isOpen()) return tiny_write_result(1, 0, false);
            const HAL_StatusTypeDef status = HAL_UART_Transmit(_huart, &c, 1, 10);
            const bool succeeded = status == HAL_OK;
            return tiny_write_result(1, succeeded ? 1 : 0, succeeded);
        }

        /**
         * @brief Transmits a buffer of bytes via HAL polling.
         */
        inline TinyWriteResult write(const uint8_t* b, size_t l) {
            if (l == 0) return tiny_write_result(0, 0, true);
            if (!isOpen() || !b || l > UINT16_MAX)
                return tiny_write_result(l, 0, false);
            const HAL_StatusTypeDef status = HAL_UART_Transmit(
                _huart, const_cast<uint8_t*>(b), static_cast<uint16_t>(l), 100);
            const bool succeeded = status == HAL_OK;
            return tiny_write_result(l, succeeded ? l : 0, succeeded);
        }

        /**
         * @brief Returns the current system tick count in milliseconds.
         */
        inline unsigned long millis() {
            return HAL_GetTick();
        }
    };

} // namespace tinylink

#endif // defined(HAL_UART_MODULE_ENABLED) && defined(STM32)
#endif // TINY_STM32_HAL_ADAPTER_H
