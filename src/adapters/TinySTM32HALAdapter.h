#ifndef TINY_STM32_HAL_ADAPTER_H
#define TINY_STM32_HAL_ADAPTER_H

#include "stm32xxxx_hal.h" // User must ensure correct family header is included
#include <stdint.h>
#include <stddef.h>

class TinySTM32HALAdapter {
private:
    UART_HandleTypeDef* _huart;

public:
    // User passes pointer to an initialized UART handle (e.g., &huart1)
    TinySTM32HALAdapter(UART_HandleTypeDef* huart) : _huart(huart) {}

    // --- TinyAdapter Interface ---
    inline bool isOpen() { 
        return (_huart != nullptr && _huart->Instance != nullptr); 
    }

    inline int available() {
        if (!isOpen()) return 0;
        // Check RX Not Empty flag directly in the Status/Interrupt Flag Register
        // Note: For some families, use __HAL_UART_GET_FLAG(_huart, UART_FLAG_RXNE)
        return (__HAL_UART_GET_FLAG(_huart, UART_FLAG_RXNE) != RESET) ? 1 : 0;
    }

    inline int read() {
        if (!isOpen()) return -1;
        uint8_t data;
        // Polling read with minimal 1ms timeout
        if (HAL_UART_Receive(_huart, &data, 1, 1) == HAL_OK) {
            return (int)data;
        }
        return -1;
    }

    inline void write(uint8_t c) {
        if (!isOpen()) return;
        HAL_UART_Transmit(_huart, &c, 1, 10);
    }

    inline void write(const uint8_t* b, size_t l) {
        if (!isOpen()) return;
        HAL_UART_Transmit(_huart, (uint8_t*)b, (uint16_t)l, 100);
    }

    inline unsigned long millis() {
        return HAL_GetTick(); // Standard HAL millisecond tick
    }
};

#endif
