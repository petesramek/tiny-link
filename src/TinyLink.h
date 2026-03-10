/**
 * @file TinyLink.h
 * @brief Main template engine for the TinyLink protocol.
 */

#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"
#include "version.h"
#include <stdint.h>
#include <string.h>

namespace tinylink {


    // -----------------------------------------------------------------------------
    //  SFINAE Adapter Validation (C++11/14 compatible)
    // -----------------------------------------------------------------------------
    //
    // This ensures the Adapter type implements:
    //   - bool isOpen()
    //   - int  available()
    //   - int  read()
    //   - void write(uint8_t)
    //   - void write(const uint8_t*, size_t)
    //   - unsigned long millis()
    // WITHOUT dereferencing a null pointer or requiring C++20 concepts.
    //

    template <typename A>
    struct is_valid_adapter {
    private:
        template <typename T>
        static auto check(int) -> decltype(
            (void) static_cast<bool>(T().isOpen()),
            (void) static_cast<int>(T().available()),
            (void) static_cast<int>(T().read()),
            (void)T().write(uint8_t(0)),
            (void)T().write((const uint8_t*)0, size_t(0)),
            (void) static_cast<unsigned long>(T().millis()),
            std::true_type{}
            );

        template <typename>
        static std::false_type check(...);

    public:
        static constexpr bool value = decltype(check<A>(0))::value;
    };

    template <typename A>
    using adapter_check_t = typename std::enable_if<is_valid_adapter<A>::value, int>::type;


    // -----------------------------------------------------------------------------
    //  TinyLink Engine Template
    // -----------------------------------------------------------------------------

    template <typename T, typename Adapter, adapter_check_t<Adapter> = 0>
    class TinyLink {
    public:
        typedef void (*ReceiverCallback)(const T& data);

    private:

        // -------------------------------------------------------------------------
        // Fletcher-16 Checksum
        // -------------------------------------------------------------------------
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

        // -------------------------------------------------------------------------
        // COBS Encoding (removes 0x00 from payload)
        // -------------------------------------------------------------------------
        size_t cobs_encode(const uint8_t* src, size_t len, uint8_t* dst) {
            size_t read_idx = 0, write_idx = 1, code_idx = 0;
            uint8_t code = 1;

            while (read_idx < len) {
                if (src[read_idx] == 0) {
                    dst[code_idx] = code;
                    code = 1;
                    code_idx = write_idx++;
                }
                else {
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

        // -------------------------------------------------------------------------
        // COBS Decoding (restores original bytes)
        // -------------------------------------------------------------------------
        size_t cobs_decode(const uint8_t* src, size_t len, uint8_t* dst) {
            size_t read_idx = 0, write_idx = 0;

            while (read_idx < len) {
                uint8_t code = src[read_idx++];

                if (code == 0 || (read_idx + code - 1) > len)
                    return 0;  // Invalid frame

                for (uint8_t i = 1; i < code; i++)
                    dst[write_idx++] = src[read_idx++];

                if (code < 0xFF && read_idx < len)
                    dst[write_idx++] = 0;
            }

            return write_idx;
        }


        // -------------------------------------------------------------------------
        // Member Variables
        // -------------------------------------------------------------------------
        Adapter* _hw;
        T _data;

        TinyStats _stats = TinyStats{};
        uint16_t _overflowErrs = 0;  // NEW

        ReceiverCallback _onReceive = nullptr;

        static const size_t PLAIN_SIZE = 3 + sizeof(T) + 2;

        uint8_t _pBuf[PLAIN_SIZE];
        uint8_t _rawBuf[PLAIN_SIZE + 64];

        uint8_t _rawIdx = 0;
        uint8_t _currType = 0;
        uint8_t _currSeq = 0;
        uint8_t _nextSeq = 0;

        bool _hasNew = false;

        unsigned long _lastByte = 0;
        unsigned long _timeout = 250;


    public:

        // -------------------------------------------------------------------------
        // Constructor
        // -------------------------------------------------------------------------
        TinyLink(Adapter& hw) : _hw(&hw) {
            static_assert(sizeof(T) <= 240, "TinyLink: Payload exceeds 240 bytes (COBS limit).");
            static_assert(alignof(T) == 1, "TinyLink: Data type must be packed. Use __attribute__((packed)).");
        }

        void onReceive(ReceiverCallback cb) { _onReceive = cb; }
        void setTimeout(unsigned long ms) { _timeout = ms; }
        void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }

        bool connected() { return _hw->isOpen(); }
        const TinyStats& getStats() { return _stats; }

        bool available() { return _hasNew; }
        const T& peek() { return _data; }
        void flush() { _hasNew = false; }

        uint8_t type() { return _currType; }
        uint8_t seq() { return _currSeq; }

        uint16_t overflowErrors() const { return _overflowErrs; }


        // -------------------------------------------------------------------------
        // Update: Main RX State Machine
        // -------------------------------------------------------------------------
        void update() {
            if (!_hw->isOpen() || _hasNew)
                return;

            if (_rawIdx > 0 && (_hw->millis() - _lastByte > _timeout)) {
                _rawIdx = 0;
                _stats.timeouts++;
            }

            while (_hw->available() > 0) {

                int incoming = _hw->read();
                if (incoming < 0) break;

                uint8_t c = (uint8_t)incoming;
                _lastByte = _hw->millis();

                // -------------------------------------------------------------
                // Frame delimiter detected (COBS frame boundary)
                // -------------------------------------------------------------
                if (c == 0x00) {
                    if (_rawIdx >= 5) {
                        size_t dLen = cobs_decode(_rawBuf, _rawIdx, _pBuf);

                        if (dLen == PLAIN_SIZE) {
                            uint16_t calc = fletcher16(_pBuf, PLAIN_SIZE - 2);

                            // Clear, explicit Fletcher-16 extraction
                            uint16_t recv =
                                uint16_t(_pBuf[PLAIN_SIZE - 2]) |
                                (uint16_t(_pBuf[PLAIN_SIZE - 1]) << 8);

                            if (calc == recv) {
                                _currType = _pBuf[0];
                                _currSeq = _pBuf[1];

                                memcpy(&_data, &_pBuf[3], sizeof(T));

                                _hasNew = true;
                                _stats.packets++;
                                _rawIdx = 0;

                                if (_onReceive)
                                    _onReceive(_data);

                                return;
                            }
                            else {
                                _stats.crcErrs++;
                            }
                        }
                        else {
                            _stats.crcErrs++;
                        }
                    }

                    _rawIdx = 0;
                }

                // -------------------------------------------------------------
                // Accumulate bytes
                // -------------------------------------------------------------
                else {
                    if (_rawIdx < sizeof(_rawBuf)) {
                        _rawBuf[_rawIdx++] = c;
                    }
                    else {
                        _overflowErrs++;
                        _rawIdx = 0;
                    }
                }
            }
        }


        // -------------------------------------------------------------------------
        // TX: Encode + Send Frame
        // -------------------------------------------------------------------------
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

            size_t eLen = cobs_encode(_pBuf, PLAIN_SIZE, _rawBuf);

            _hw->write(0x00);
            _hw->write(_rawBuf, eLen);
            _hw->write(0x00);

            _rawIdx = 0;
        }
    };

} // namespace tinylink

#endif // TINY_LINK_H
