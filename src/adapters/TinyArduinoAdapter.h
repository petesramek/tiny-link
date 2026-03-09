/**
 * @file TinyArduinoAdapter.h
 * @brief Arduino framework adapter for TinyLink.
 */

#ifndef TINY_ARDUINO_ADAPTER_H
#define TINY_ARDUINO_ADAPTER_H

#ifdef ARDUINO
#include <Arduino.h>

namespace tinylink {

    /**
     * @class TinyArduinoAdapter
     * @brief Wraps Arduino Stream-based objects (Serial, SoftwareSerial) for TinyLink.
     */
    class TinyArduinoAdapter {
    private:
        Stream* _port;

    public:
        /**
         * @brief Construct a new Tiny Arduino Adapter.
         * @param port Reference to an Arduino Stream object (e.g., Serial).
         */
        TinyArduinoAdapter(Stream& port) : _port(&port) {}

        /** @brief Returns true if the port pointer is valid. */
        inline bool isOpen()   { return _port != nullptr; }

        /** @brief Returns number of bytes available in the RX buffer. */
        inline int available() { return _port->available(); }

        /** @brief Reads a single byte from the RX buffer. */
        inline int read()      { return _port->read(); }

        /** @brief Writes a single byte to the TX line. */
        inline void write(uint8_t c) { _port->write(c); }

        /** @brief Writes a buffer of bytes to the TX line. */
        inline void write(const uint8_t* b, size_t l) { _port->write(b, l); }

        /** @brief Returns the current system uptime in milliseconds. */
        inline unsigned long millis() { return ::millis(); }
    };
}

#endif // ARDUINO
#endif // TINY_ARDUINO_ADAPTER_H
