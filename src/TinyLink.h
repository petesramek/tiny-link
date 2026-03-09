#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"
#include "version.h"

namespace tinylink {

template <typename T, typename Adapter>
class TinyLink {
public:
    typedef void (*ReceiverCallback)(const T& data);

private:
    // --- Compile-Time Driver Validation ---
    void _static_interface_check() {
        Adapter* a = (Adapter*)0;
        (void)a->isOpen(); (void)a->available(); (void)a->read(); (void)a->millis(); a->write((uint8_t)0);
    }

    // --- Fletcher-16 Checksum ---
    inline uint16_t fletcher16(const uint8_t* data, uint8_t len) {
        uint16_t sum1 = 0xff, sum2 = 0xff;
        while (len) {
            uint8_t tlen = len > 20 ? 20 : len;
            len -= tlen;
            do { sum1 += *data++; sum2 += sum1; } while (--tlen);
            sum1 = (sum1 & 0xff) + (sum1 >> 8);
            sum2 = (sum2 & 0xff) + (sum2 >> 8);
        }
        return (sum2 << 8) | sum1;
    }

    // --- COBS Encoding ---
    size_t cobs_encode(const uint8_t* src, size_t len, uint8_t* dst) {
        size_t read_idx = 0, write_idx = 1, code_idx = 0;
        uint8_t code = 1;
        while (read_idx < len) {
            if (src[read_idx] == 0) {
                dst[code_idx] = code;
                code = 1;
                code_idx = write_idx++;
            } else {
                dst[write_idx++] = src[read_idx];
                code++;
                if (code == 0xFF) {
                    dst[code_idx] = code;
                    code = 1;
                    code_idx = write_idx++;
                }
            }
            read_idx++;
        }
        dst[code_idx] = code;
        return write_idx;
    }

    // --- COBS Decoding ---
    size_t cobs_decode(const uint8_t* src, size_t len, uint8_t* dst) {
        size_t read_idx = 0, write_idx = 0;
        while (read_idx < len) {
            uint8_t code = src[read_idx++];
            if (read_idx + code - 1 > len) return 0; 
            for (uint8_t i = 1; i < code; i++) dst[write_idx++] = src[read_idx++];
            if (code < 0xFF && read_idx < len) dst[write_idx++] = 0;
        }
        return write_idx;
    }

    Adapter* _hw;
    T _data;                         // Live, verified data
    TinyStats _stats = {0, 0, 0};
    ReceiverCallback _onReceive = nullptr;

    // --- Memory Optimized Buffers ---
    static const size_t PLAIN_SIZE = 3 + sizeof(T) + 2; 
    uint8_t _pBuf[PLAIN_SIZE];       // "Work Buffer" for decoding/integrity checks
    uint8_t _rawBuf[PLAIN_SIZE + 2]; // Encoded buffer
    uint8_t _rawIdx = 0;

    uint8_t _currType = 0, _currSeq = 0, _nextSeq = 0;
    bool _hasNew = false;
    unsigned long _lastByte = 0;
    unsigned long _timeout = 250;

public:
    TinyLink(Adapter& hw) : _hw(&hw) {
        static_assert(sizeof(T) <= 240, "TinyLink: Payload exceeds 240 bytes (COBS limit).");
        (void)&TinyLink::_static_interface_check;
    }

    // --- Config & Lifecycle ---
    void onReceive(ReceiverCallback cb) { _onReceive = cb; }
    void setTimeout(unsigned long ms) { _timeout = ms; }
    void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }

    // --- Status & Data ---
    bool connected() { return _hw->isOpen(); }
    const TinyStats& getStats() { return _stats; }
    bool available() { return _hasNew; }
    const T& peek() { return _data; }
    void flush() { _hasNew = false; }
    uint8_t type() { return _currType; }
    uint8_t seq()  { return _currSeq; }

    // --- Core Engine ---
    void update() {
        if (!_hw->isOpen() || _hasNew) return;

        if (_rawIdx > 0 && (_hw->millis() - _lastByte > _timeout)) {
            _rawIdx = 0;
            _stats.timeouts++;
        }

        while (_hw->available() > 0) {
            int incoming = _hw->read();
            if (incoming < 0) break;
            uint8_t c = (uint8_t)incoming;
            _lastByte = _hw->millis();

            if (c == 0x00) { 
                if (_rawIdx >= 5) {
                    // Decode into "Work Buffer" to isolate from live data
                    size_t dLen = cobs_decode(_rawBuf, _rawIdx, _pBuf);
                    
                    if (dLen == PLAIN_SIZE) {
                        uint16_t calc = fletcher16(_pBuf, PLAIN_SIZE - 2);
                        uint16_t recv = (uint16_t)_pBuf[PLAIN_SIZE - 2] | ((uint16_t)_pBuf[PLAIN_SIZE - 1] << 8);
                        
                        if (calc == recv) {
                            // Only overwrite live data AFTER verification
                            _currType = _pBuf[0];
                            _currSeq  = _pBuf[1];
                            memcpy(&_data, &_pBuf[3], sizeof(T));
                            
                            _hasNew = true;
                            _stats.packets++;
                            _rawIdx = 0;

                            if (_onReceive) _onReceive(_data);
                            return; 
                        } else { _stats.crcErrs++; }
                    } else { _stats.crcErrs++; }
                }
                _rawIdx = 0;
            } else {
                if (_rawIdx < sizeof(_rawBuf)) _rawBuf[_rawIdx++] = c;
                else _rawIdx = 0; 
            }
        }
    }

    void send(uint8_t type, const T& payload) {
        if (!_hw->isOpen()) return;
        
        // Prepare plain frame in Work Buffer to save stack RAM
        _pBuf[0] = type;
        _pBuf[1] = _nextSeq++;
        _pBuf[2] = (uint8_t)sizeof(T);
        memcpy(&_pBuf[3], &payload, sizeof(T));

        uint16_t chk = fletcher16(_pBuf, PLAIN_SIZE - 2);
        _pBuf[PLAIN_SIZE - 2] = chk & 0xFF;
        _pBuf[PLAIN_SIZE - 1] = (chk >> 8) & 0xFF;

        // Encode from Work Buffer into Raw Buffer
        uint8_t encoded[PLAIN_SIZE + 2];
        size_t eLen = cobs_encode(_pBuf, PLAIN_SIZE, encoded);

        _hw->write(0x00);
        _hw->write(encoded, eLen);
        _hw->write(0x00);
    }
};

} // namespace tinylink

#endif
