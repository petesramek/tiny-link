#ifndef TINY_TEST_ADAPTER_H
#define TINY_TEST_ADAPTER_H

#include <vector>
#include <stdint.h>
#include <stddef.h>

class TinyTestAdapter {
protected: // Changed to protected so LoopbackAdapter can access _buffer
    std::vector<uint8_t> _buffer;
    unsigned long _fakeMillis = 0;

public:
    virtual ~TinyTestAdapter() {} // Good practice for virtual classes

    void setMillis(unsigned long m) { _fakeMillis = m; }
    void advanceMillis(unsigned long m) { _fakeMillis += m; }
    
    void inject(const uint8_t* data, size_t len) {
        _buffer.insert(_buffer.end(), data, data + len);
    }

    std::vector<uint8_t>& getRawBuffer() { return _buffer; }

    // --- TinyAdapter Interface (Now Virtual) ---
    virtual bool isOpen() { return true; }
    virtual int available() { return (int)_buffer.size(); }
    
    virtual int read() {
        if (_buffer.empty()) return -1;
        uint8_t b = _buffer.front();
        _buffer.erase(_buffer.begin());
        return (int)b;
    }

    virtual void write(uint8_t c) { (void)c; }
    virtual void write(const uint8_t* b, size_t l) { (void)b; (void)l; }
    
    virtual unsigned long millis() { return _fakeMillis; }
};

#endif
