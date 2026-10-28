/**
 * @file adapters/TinySTM32HALAdapter.h
 * @brief TinyLink transport adapter for an STM32 HAL UART handle.
 *
 * The adapter is non-owning. The supplied UART handle and its peripheral
 * configuration must remain valid for the entire lifetime of the adapter and
 * every TinyLink instance that uses it.
 *
 * The application must include the correct STM32 family HAL header before this
 * file so UART_HandleTypeDef, HAL UART functions, and UART flags are available.
 *
 * HAL transmit statuses are exposed as TinyLink write results. HAL's
 * synchronous transmit API reports full success or failure, not partial counts.
 */
#ifndef TINYLINK_ADAPTERS_TINYSTM32HALADAPTER_H
#define TINYLINK_ADAPTERS_TINYSTM32HALADAPTER_H

#if defined(HAL_UART_MODULE_ENABLED) && defined(STM32)

#include <stddef.h>
#include <stdint.h>

#include "adapters/WriteResult.h"

namespace tinylink {

    /**
     * @brief Non-owning polling adapter over an STM32 HAL UART handle.
     *
     * The adapter does not initialize, configure, start, stop, or own the UART.
     * Reads and writes use blocking HAL polling calls with bounded timeouts.
     */
    class TinySTM32HALAdapter {
    private:
        UART_HandleTypeDef* _uart;

    public:
        /**
         * @brief Construct an adapter over an existing STM32 HAL UART handle.
         *
         * @param uart Initialized UART handle. The caller must keep the handle and
         * its Instance valid for the lifetime of this adapter.
         */
        explicit TinySTM32HALAdapter(
            UART_HandleTypeDef* uart)
            : _uart(uart)
        {}

        /**
         * @brief Determine whether a UART handle and peripheral instance are bound.
         *
         * This check does not verify peripheral clocks, GPIO configuration, UART
         * state, or physical connectivity.
         *
         * @return true when the handle and its Instance pointer are non-null.
         */
        inline bool isOpen() const
        {
            return _uart != nullptr &&
                _uart->Instance != nullptr;
        }

        /**
         * @brief Determine whether the UART receive-data register is nonempty.
         *
         * The adapter exposes only zero or one because it does not own a software
         * receive buffer. The flag may change before read() executes.
         *
         * @return 1 when RXNE is set; otherwise zero.
         */
        inline int available() const
        {
            if (!isOpen()) {
                return 0;
            }

            return __HAL_UART_GET_FLAG(
                _uart,
                UART_FLAG_RXNE) != RESET
                ? 1
                : 0;
        }

        /**
         * @brief Read one byte using HAL polling with a one-millisecond timeout.
         *
         * HAL timeout and error statuses are represented as -1 by the current
         * TinyLink adapter contract.
         *
         * @return Byte in the range 0 through 255, or -1 when no byte was read.
         */
        inline int read()
        {
            if (!isOpen()) {
                return -1;
            }

            uint8_t value = 0;

            if (HAL_UART_Receive(
                _uart,
                &value,
                1,
                1) != HAL_OK) {
                return -1;
            }

            return static_cast<int>(value);
        }

        /**
         * @brief Transmit one byte using HAL polling.
         *
         * The call may block for up to ten milliseconds.
         *
         * @param value Byte to transmit.
         * @return Complete result on HAL_OK; otherwise failed.
         */
        inline WriteResult write(uint8_t value)
        {
            if (!isOpen()) {
                return WriteResult::failure();
            }

            return HAL_UART_Transmit(
                    _uart,
                    &value,
                    1,
                    10) == HAL_OK
                ? WriteResult::fromCount(1, 1)
                : WriteResult::failure();
        }

        /**
         * @brief Transmit a byte sequence using HAL polling.
         *
         * The call may block for up to 100 milliseconds. STM32 HAL accepts a
         * 16-bit transfer length, so larger requests are rejected rather than
         * truncated. A null pointer is valid only when @p length is zero.
         *
         * @param data Bytes to transmit, or nullptr when @p length is zero.
         * @param length Number of bytes to transmit.
         * @return Complete result on HAL_OK; otherwise failed or zero-length.
         */
        inline WriteResult write(
            const uint8_t* data,
            size_t length)
        {
            if (length == 0) {
                return WriteResult::fromCount(0, 0);
            }

            if (!isOpen() || !data || length > UINT16_MAX) {
                return WriteResult::failure();
            }

            return HAL_UART_Transmit(
                    _uart,
                    const_cast<uint8_t*>(data),
                    static_cast<uint16_t>(length),
                    100) == HAL_OK
                ? WriteResult::fromCount(length, length)
                : WriteResult::failure();
        }

        /**
         * @brief Return the STM32 HAL system tick in milliseconds.
         *
         * HAL_GetTick() normally wraps at the width of uint32_t. TinyLink uses
         * unsigned elapsed-time subtraction, which supports this rollover.
         *
         * @return Current HAL system tick in milliseconds.
         */
        inline unsigned long millis() const
        {
            return static_cast<unsigned long>(
                HAL_GetTick());
        }
    };

} // namespace tinylink

#endif // defined(HAL_UART_MODULE_ENABLED) && defined(STM32)

#endif // TINYLINK_ADAPTERS_TINYSTM32HALADAPTER_H
