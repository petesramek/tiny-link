#ifndef TINY_TEST_ADAPTER_H
#define TINY_TEST_ADAPTER_H

#include <vector>
#include <stdint.h>

class TinyTestAdapter {
private:
    std::vector<uint8_t> _buffer;
    unsigned long _fakeMillis = 0;

public:
    // Manual time control for testing timeouts
    void setMillis(unsigned long m) { _fakeMillis = m; }
    void advanceMillis(unsigned long m) { _fakeMillis += m; }
    
    // Inject data for the Link to "receive"
    void inject(const uint8_t* data, size_t len) {
        _buffer.insert(_buffer.end(), data, data + len);
    }

    // --- TinyAdapter Interface ---
    int available() { return (int)_buffer.size(); }
    
    int read() {
        if (_buffer.empty()) return -1;
        uint8_t b = _buffer.front();
        _buffer.erase(_buffer.begin());
        return b;
    }

    void write(uint8_t c) { /* Can be logged for verification */ }
    void write(const uint8_t* b, size_t l) { /* Can be logged for verification */ }
    
    unsigned long millis() { return _fakeMillis; }
};

#endif
