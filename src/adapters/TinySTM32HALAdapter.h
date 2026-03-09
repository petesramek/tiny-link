#ifndef TINY_STM32_HAL_ADAPTER_H
#define TINY_STM32_HAL_ADAPTER_H

#include "stm32f4xx_hal.h" // Replace with your specific family header

class TinySTM32HALAdapter {
private:
    UART_HandleTypeDef* _huart;

public:
    TinySTM32HALAdapter(UART_HandleTypeDef* huart) : _huart(huart) {}

    inline int available() {
        // Checks if the RXne (Receive Not Empty) flag is set in the status register
        return (__HAL_UART_GET_FLAG(_huart, UART_FLAG_RXNE) != RESET) ? 1 : 0;
    }

    inline int read() {
        uint8_t byte;
        // Basic polling read; for production, use a circular buffer + Interrupts
        if (HAL_UART_Receive(_huart, &byte, 1, 10) == HAL_OK) return byte;
        return -1;
    }

    inline void write(uint8_t c) {
        HAL_UART_Transmit(_huart, &c, 1, 10);
    }

    inline void write(const uint8_t* b, size_t l) {
        HAL_UART_Transmit(_huart, (uint8_t*)b, l, 100);
    }

    inline unsigned long millis() {
        return HAL_GetTick(); // Native STM32 millisecond tick
    }
};
#endif
