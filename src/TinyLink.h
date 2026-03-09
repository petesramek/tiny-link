#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"
#include <type_traits>

template <typename T, typename Adapter>
class TinyLink {
private:
    // --- Compile-Time Driver Validation ---
    void _static_interface_check() {
        Adapter* a = nullptr;
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
    
    bool _hasNew = false;
    unsigned long _lastByte = 0;
    unsigned long _timeout = 250;

    inline uint8_t checksum(const uint8_t* buf, uint8_t len) {
        uint8_t sum = 0;
        for(uint8_t i = 0; i < len; i++) sum += buf[i];
        return sum;
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
    
    // --- Hardware & Status ---
    bool connected() { return _hw->isOpen(); }
    const TinyStats& getStats() { return _stats; }
    void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }
    TinyStatus getStatus() { return _status; }
    TinyState getState() { return _state; }
    void setTimeout(unsigned long ms) { _timeout = ms; }

    // --- Data Access ---
    bool available() { return _hasNew; }
    const T& peek() { return _data; }
    void flush() { _hasNew = false; }
    
    uint8_t type() { return _currType; }
    uint8_t seq()  { return _currSeq; }

    // --- Core Engine ---
    void update() {
        if (!_hw->isOpen()) return;

        // Flow Control: Don't fetch new bytes if a packet is waiting for flush()
        if (_hasNew) return;

        // Timeout Logic
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
                    if (c == SOH) {
                        _status = TinyStatus::STATUS_OK;
                        _state = TinyState::IN_TYPE;
                    } 
                    break;
                case TinyState::IN_TYPE:
                    _currType = _hBuf[0] = c;
                    _state = TinyState::WAIT_SEQ;
                    break;
                case TinyState::WAIT_SEQ:
                    _currSeq = _hBuf[1] = c;
                    _state = TinyState::WAIT_LEN;
                    break;
                case TinyState::WAIT_LEN: 
                    _expectedLen = _hBuf[2] = c; 
                    if (_expectedLen > sizeof(T)) {
                        reset(TinyStatus::ERR_CRC, NAK);
                        return;
                    }
                    _state = TinyState::WAIT_H_CHK; 
                    break;
                case TinyState::WAIT_H_CHK: 
                    if (c == checksum(_hBuf, 3)) {
                        _state = TinyState::WAIT_STX;
                    } else {
                        reset(TinyStatus::ERR_CRC, NAK);
                    }
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
                    if (c == ETX) _state = TinyState::WAIT_P_CHK; 
                    else reset(TinyStatus::ERR_CRC, CAN);
                    break;
                case TinyState::WAIT_P_CHK: 
                    if (c == checksum(_pBuf, _expectedLen)) {
                        _state = TinyState::WAIT_ACK;
                    } else {
                        reset(TinyStatus::ERR_CRC, NAK);
                    } 
                    break;
                case TinyState::WAIT_ACK: 
                    if (c == ACK) { 
                        memcpy(&_data, _pBuf, sizeof(T)); 
                        _hasNew = true; 
                        _stats.packets++; 
                        return;
                    }
                    _state = TinyState::WAIT_SOH;
                    break;
            }
        }
    }

    void send(uint8_t type, const T& payload) {
        if (!_hw->isOpen()) return;
        uint8_t len = sizeof(T);
        const uint8_t* pRaw = (const uint8_t*)&payload;
        uint8_t hData[3] = { type, _nextSeq++, len };
        
        _hw->write(SOH);
        _hw->write(hData, 3);
        _hw->write(checksum(hData, 3));
        _hw->write(STX);
        _hw->write(pRaw, len);
        _hw->write(ETX);
        _hw->write(checksum(pRaw, len));
        _hw->write(ACK);
    }

    TinyStatus getStatus() {
        return _status;
    }

    TinyState getState() {
        return _state;
    }

    void clearStats() { 
        memset(&_stats, 0, sizeof(TinyStats));
    }
};
#endif
