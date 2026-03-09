/**
 * @file TinyLink.h
 * @brief Main template engine for the TinyLink protocol.
 */

#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"
#include "version.h"

namespace tinylink {

/**
 * @brief High-efficiency, template-based serial protocol engine.
 * 
 * @tparam T The struct type to be transmitted. Must be a POD (Plain Old Data) type.
 * @tparam Adapter The hardware abstraction layer (e.g., TinyArduinoAdapter).
 */
template <typename T, typename Adapter>
class TinyLink {
public:
    /**
     * @brief Signature for the asynchronous reception callback.
     * @param data A constant reference to the verified received struct.
     */
    typedef void (*ReceiverCallback)(const T& data);

private:
    void _static_interface_check() {
        Adapter* a = (Adapter*)0;
        (void)a->isOpen(); (void)a->available(); (void)a->read(); (void)a->millis(); a->write((uint8_t)0);
    }

    /** @brief Fletcher-16 Checksum: Optimized for 8-bit CPUs. */
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

    /** @brief COBS Encoding: Eliminates 0x00 from payload. */
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

    /** @brief COBS Decoding: Restores original data from encoded frame. */
    size_t cobs_decode(const uint8_t* src, size_t len, uint8_t* dst) {
        size_t read_idx = 0, write_idx = 0;
        while (read_idx < len) {
            uint8_t code = src[read_idx++];
            // Safety: If code is 0 (invalid) or points past the buffer, stop.
            if (code == 0 || (read_idx + code - 1) > len) return 0; 
            
            for (uint8_t i = 1; i < code; i++) {
                dst[write_idx++] = src[read_idx++];
            }
            if (code < 0xFF && read_idx < len) {
                dst[write_idx++] = 0;
            }
        }
        return write_idx;
    }

    Adapter* _hw;
    T _data;                         
    TinyStats _stats = {0, 0, 0};
    ReceiverCallback _onReceive = nullptr;
     
    static const size_t PLAIN_SIZE = 3 + sizeof(T) + 2; 
    uint8_t _pBuf[PLAIN_SIZE];     
    uint8_t _rawBuf[PLAIN_SIZE + 64]; 
    uint8_t _rawIdx = 0;

    uint8_t _currType = 0, _currSeq = 0, _nextSeq = 0;
    bool _hasNew = false;
    unsigned long _lastByte = 0;
    unsigned long _timeout = 250;

public:
    /**
     * @brief Construct a new TinyLink object.
     * @param hw Reference to a hardware adapter (e.g., TinyArduinoAdapter).
     */
    TinyLink(Adapter& hw) : _hw(&hw) {
        static_assert(sizeof(T) <= 240, "TinyLink: Payload exceeds 240 bytes (COBS limit).");
        static_assert(alignof(T) == 1, "TinyLink: Data type must be packed (alignof == 1). Use  __attribute__((packed)) on data type.");
        (void)&TinyLink::_static_interface_check;
    }

    /** @brief Registers a callback to be executed upon valid packet reception. */
    void onReceive(ReceiverCallback cb) { _onReceive = cb; }
    
    /** @brief Sets the inter-byte timeout for frame accumulation. */
    void setTimeout(unsigned long ms) { _timeout = ms; }
    
    /** @brief Resets packet, error, and timeout counters. */
    void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }

    /** @brief Returns true if the hardware adapter is open/connected. */
    bool connected() { return _hw->isOpen(); }
    
    /** @brief Returns a reference to the current link statistics. */
    const TinyStats& getStats() { return _stats; }
    
    /** @brief Checks if a new verified packet is available to peek. */
    bool available() { return _hasNew; }
    
    /** @brief Returns a constant reference to the most recent valid payload. */
    const T& peek() { return _data; }
    
    /** @brief Clears the available flag to allow for the next packet. */
    void flush() { _hasNew = false; }
    
    /** @brief Returns the message type of the current packet. */
    uint8_t type() { return _currType; }
    
    /** @brief Returns the sequence number of the current packet. */
    uint8_t seq()  { return _currSeq; }

    /**
     * @brief Processes incoming serial data and manages the state machine.
     * Must be called frequently in the main loop.
     */
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
                    size_t dLen = cobs_decode(_rawBuf, _rawIdx, _pBuf);
                    
                    if (dLen == PLAIN_SIZE) {
                        uint16_t calc = fletcher16(_pBuf, PLAIN_SIZE - 2);
                        uint16_t recv = (uint16_t)_pBuf[PLAIN_SIZE - 2] | ((uint16_t)_pBuf[PLAIN_SIZE - 1] << 8);
                        
                        if (calc == recv) {
                            _currType = _pBuf[0];
                            _currSeq  = _pBuf[1];
                            memcpy(&_data, &_pBuf[3], sizeof(T));
                            
                            _hasNew = true;
                            _stats.packets++;
                            _rawIdx = 0;

                            if (_onReceive) _onReceive(_data);
                            return; 
                        } else { 
                            _stats.crcErrs++; // Bad Checksum
                        }
                    } else { 
                        _stats.crcErrs++; // Valid COBS but wrong length (Structural Error)
                    }
                }
                // Note: We don't increment crcErrs if _rawIdx < 5 
                // because that is likely just a noise spike or double 0x00.
                _rawIdx = 0; 
            } else {
                if (_rawIdx < sizeof(_rawBuf)) _rawBuf[_rawIdx++] = c;
                else _rawIdx = 0; 
            }
        }
    }

    /**
     * @brief Encodes and transmits the provided payload.
     * @param type User-defined message type.
     * @param payload The data struct to transmit.
     */
    void send(uint8_t type, const T& payload) {
        if (!_hw->isOpen()) return;
        
        _currSeq = _nextSeq++; 

        _pBuf[0] = type;
        _pBuf[1] = _currSeq;
        _pBuf[2] = (uint8_t)sizeof(T);
        memcpy(&_pBuf[3], &payload, sizeof(T));

        uint16_t chk = fletcher16(_pBuf, PLAIN_SIZE - 2);
        _pBuf[PLAIN_SIZE - 2] = chk & 0xFF;
        _pBuf[PLAIN_SIZE - 1] = (chk >> 8) & 0xFF;

        // Reuse _rawBuf to save stack RAM
        size_t eLen = cobs_encode(_pBuf, PLAIN_SIZE, _rawBuf);

        _hw->write(0x00);
        _hw->write(_rawBuf, eLen);
        _hw->write(0x00);
        
        _rawIdx = 0; // Ensure receiver state is clean
    }
};

} // namespace tinylink

#endif // TINY_LINK_H
