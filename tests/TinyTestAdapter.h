#ifndef TINY_TEST_ADAPTER_H
#define TINY_TEST_ADAPTER_H
#include <vector>

class TinyTestAdapter {
private:
    std::vector<uint8_t> _buf;
    unsigned long _t = 0;
public:
    inline bool isOpen()   { return true; }
    inline void setMillis(unsigned long m) { _t = m; }
    inline void inject(const uint8_t* d, size_t l) { _buf.insert(_buf.end(), d, d + l); }
    inline int available() { return (int)_buf.size(); }
    inline int read()      { if(_buf.empty()) return -1; uint8_t b = _buf[0]; _buf.erase(_buf.begin()); return b; }
    inline void write(uint8_t c) {}
    inline void write(const uint8_t* b, size_t l) {}
    inline unsigned long millis() { return _t; }
};
#endif
