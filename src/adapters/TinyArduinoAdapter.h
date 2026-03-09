#ifndef TINY_ARDUINO_ADAPTER_H
#define TINY_ARDUINO_ADAPTER_H
#ifdef ARDUINO
#include <Arduino.h>

namespace tinylink {
    class TinyArduinoAdapter {
    private:
        Stream* _port;
    public:
        TinyArduinoAdapter(Stream& port) : _port(&port) {}
        inline bool isOpen()   { return _port != nullptr; }
        inline int available() { return _port->available(); }
        inline int read()      { return _port->read(); }
        inline void write(uint8_t c) { _port->write(c); }
        inline void write(const uint8_t* b, size_t l) { _port->write(b, l); }
        inline unsigned long millis() { return ::millis(); }
    };
}

#endif
#endif