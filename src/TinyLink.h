#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"

template <typename T, typename Adapter>
class TinyLink {
private:
    void _static_interface_check() {
        Adapter* a = (Adapter*)0;
        (void)a->isOpen();
        (void)a->available();
        (void)a->read();
        (void)a->millis();
        a->write((uint8_t)0);
    }

    Adapter* _hw;
    T _data;
    TinyStats _stats = {0, 0, 0};
    
    TinyState _state = TinyState::WAIT_SOH;
    TinyStatus _status = TinyStatus::STATUS_OK;
    
    uint8_t _hBuf[3]; 
    uint8_t _pBuf[sizeof(T)];
    uint8_t _pIdx = 0, _expectedLen = 0, _currType = 0, _currSeq = 0, _nextSeq = 0;
    
    // New: Buffer to capture 16-bit checksum bytes
    uint16_t _chkBuffer = 0; 

    bool _hasNew = false;
    unsigned long _lastByte = 0;
    unsigned long _timeout = 250;

    // Fletcher-16 Checksum: Optimized for 8-bit architectures
    inline uint16_t fletcher16(const uint8_t* data, uint8_t len) {
        uint16_t sum1 = 0xff, sum2 = 0xff;
        while (len) {
            uint8_t tlen = len > 20 ? 20 : len;
            len -= tlen;
            do {
                sum1 += *data++;
                sum2 += sum1;
            } while (--tlen);
            sum1 = (sum1 & 0xff) + (sum1 >> 8);
            sum2 = (sum2 & 0xff) + (sum2 >> 8);
        }
        return (sum2 << 8) | sum1;
    }

    void reset(TinyStatus s, uint8_t signal = 0) {
        if (_hw->isOpen() && signal) _hw->write(signal);
        if (s == TinyStatus::ERR_TIMEOUT) _stats.timeouts++;
        if (s == TinyStatus::ERR_CRC)     _stats.crcErrs++;
        _status = s;
        _state = TinyState::WAIT_SOH;
        _pIdx = 0;
    }

public:
    TinyLink(Adapter& hw) : _hw(&hw) { 
        static_assert(sizeof(T) <= 255, "Payload exceeds 255 byte limit.");
        (void)&TinyLink::_static_interface_check; 
    }
    
    bool connected() { return _hw->isOpen(); }
    const TinyStats& getStats() { return _stats; }
    void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }
    TinyStatus getStatus() { return _status; }
    TinyState getState() { return _state; }
    void setTimeout(unsigned long ms) { _timeout = ms; }

    bool available() { return _hasNew; }
    const T& peek() { return _data; }
    void flush() { _hasNew = false; }
    
    uint8_t type() { return _currType; }
    uint8_t seq()  { return _currSeq; }

    void update() {
        if (!_hw->isOpen() || _hasNew) return;

        if (_state != TinyState::WAIT_SOH && (_hw->millis() - _lastByte > _timeout)) {
            reset(TinyStatus::ERR_TIMEOUT, CAN);
            return;
        }

        while (_hw->available() > 0) {
            int incoming = _hw->read();
            if (incoming < 0) break;
            uint8_t c = (uint8_t)incoming;
            _lastByte = _hw->millis();

            switch (_state) {
                case TinyState::WAIT_SOH: 
                    if (c == SOH) { _status = TinyStatus::STATUS_OK; _state = TinyState::IN_TYPE; } 
                    break;
                case TinyState::IN_TYPE:  _currType = _hBuf[0] = c; _state = TinyState::WAIT_SEQ; break;
                case TinyState::WAIT_SEQ: _currSeq  = _hBuf[1] = c; _state = TinyState::WAIT_LEN; break;
                case TinyState::WAIT_LEN: 
                    _expectedLen = _hBuf[2] = c; 
                    if (_expectedLen > sizeof(T)) { reset(TinyStatus::ERR_CRC, NAK); return; }
                    _state = TinyState::WAIT_H_CHK_L; 
                    break;

                // Capture 16-bit Header Checksum
                case TinyState::WAIT_H_CHK_L: _chkBuffer = c; _state = TinyState::WAIT_H_CHK_H; break;
                case TinyState::WAIT_H_CHK_H: 
                    _chkBuffer |= (uint16_t)c << 8;
                    if (_chkBuffer == fletcher16(_hBuf, 3)) _state = TinyState::WAIT_STX;
                    else reset(TinyStatus::ERR_CRC, NAK);
                    break;

                case TinyState::WAIT_STX: 
                    if (c == STX) { _pIdx = 0; _state = TinyState::IN_PAYLOAD; } 
                    else reset(TinyStatus::ERR_CRC, CAN);
                    break;
                case TinyState::IN_PAYLOAD: 
                    if (_pIdx < _expectedLen) _pBuf[_pIdx++] = c;
                    if (_pIdx >= _expectedLen) _state = TinyState::WAIT_ETX;
                    break;
                case TinyState::WAIT_ETX: 
                    if (c == ETX) _state = TinyState::WAIT_P_CHK_L; 
                    else reset(TinyStatus::ERR_CRC, CAN);
                    break;

                // Capture 16-bit Payload Checksum
                case TinyState::WAIT_P_CHK_L: _chkBuffer = c; _state = TinyState::WAIT_P_CHK_H; break;
                case TinyState::WAIT_P_CHK_H: 
                    _chkBuffer |= (uint16_t)c << 8;
                    if (_chkBuffer == fletcher16(_pBuf, _expectedLen)) _state = TinyState::WAIT_ACK;
                    else reset(TinyStatus::ERR_CRC, NAK);
                    break;

                case TinyState::WAIT_ACK: 
                    if (c == ACK) { 
                        memcpy(&_data, _pBuf, sizeof(T)); 
                        _hasNew = true; _stats.packets++; 
                        _state = TinyState::WAIT_SOH; return; 
                    }
                    _state = TinyState::WAIT_SOH;
                    break;
            }
        }
    }

    void send(uint8_t type, const T& payload) {
        if (!_hw->isOpen()) return;
        uint8_t hData[3] = { type, _nextSeq++, (uint8_t)sizeof(T) };
        uint16_t hChk = fletcher16(hData, 3);
        uint16_t pChk = fletcher16((const uint8_t*)&payload, sizeof(T));
        
        _hw->write(SOH);
        _hw->write(hData, 3);
        _hw->write(hChk & 0xFF); _hw->write((hChk >> 8) & 0xFF); // Little-endian
        _hw->write(STX);
        _hw->write((const uint8_t*)&payload, sizeof(T));
        _hw->write(ETX);
        _hw->write(pChk & 0xFF); _hw->write((pChk >> 8) & 0xFF); // Little-endian
        _hw->write(ACK);
    }
};

#endif
