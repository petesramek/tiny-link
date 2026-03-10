/**
 * @file TinyArduinoAdapter.h
 * @brief Arduino framework adapter for TinyLink.
 *
 * This adapter wraps any Arduino Stream-based object (e.g. Serial,
 * SoftwareSerial, HardwareSerial) and exposes a minimal interface used by
 * tinylink::TinyLink.
 */

#ifndef TINY_ARDUINO_ADAPTER_H
#define TINY_ARDUINO_ADAPTER_H

#ifdef ARDUINO

#include <Arduino.h>
#include <stdint.h>

namespace tinylink {

    /**
     * @class TinyArduinoAdapter
     * @brief Wraps Arduino Stream-based objects (Serial, SoftwareSerial, etc.) for TinyLink.
     *
     * Adapter contract (required by TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - void          write(uint8_t);
     *   - void          write(const uint8_t*, size_t);
     *   - unsigned long millis();
     */
    class TinyArduinoAdapter {
    private:
        Stream* _port;

    public:
        /**
         * @brief Construct a new TinyArduinoAdapter.
         * @param port Reference to an Arduino Stream object (e.g., Serial).
         */
        explicit TinyArduinoAdapter(Stream& port)
            : _port(&port) {
        }

        /**
         * @brief Returns true if the underlying port pointer is valid.
         */
        inline bool isOpen() {
            return _port != nullptr;
        }

        /**
         * @brief Returns the number of bytes available in the RX buffer.
         */
        inline int available() {
            return (_port != nullptr) ? _port->available() : 0;
        }

        /**
         * @brief Reads a single byte from the RX buffer.
         * @return Byte in range [0,255], or -1 if no data is available.
         */
        inline int read() {
            return (_port != nullptr) ? _port->read() : -1;
        }

        /**
         * @brief Writes a single byte to the TX line.
         */
        inline void write(uint8_t c) {
            if (_port) _port->write(c);
        }

        /**
         * @brief Writes a buffer of bytes to the TX line.
         */
        inline void write(const uint8_t* b, size_t l) {
            if (_port && b && l > 0) _port->write(b, l);
        }

        /**
         * @brief Returns the current system uptime in milliseconds.
         */
        inline unsigned long millis() {
            return ::millis();
        }
    };

} // namespace tinylink

#endif // ARDUINO
#endif // TINY_ARDUINO_ADAPTER_H
