#ifndef TINYLINK_TEST_ARDUINO_STUB_H
#define TINYLINK_TEST_ARDUINO_STUB_H

#include <stddef.h>
#include <stdint.h>

static unsigned long tinylinkArduinoStubMillis = 0;

class Stream {
public:
    virtual ~Stream() {}
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t* data, size_t length) = 0;
};

inline unsigned long millis()
{
    return tinylinkArduinoStubMillis;
}

#endif // TINYLINK_TEST_ARDUINO_STUB_H
