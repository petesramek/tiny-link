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

#ifndef __AVR__
#include <type_traits>
#endif

namespace tinylink {

#ifndef __AVR__
    // -----------------------------------------------------------------------------
    //  SFINAE Adapter Validation (C++11/14 compatible, non-AVR only)
    // -----------------------------------------------------------------------------
    //
    // avr-gcc does not ship <type_traits>, so this block is skipped on AVR targets.
    // On desktop/ESP builds, this enforces the adapter contract at compile time.
    // std::declval<T>() is used so the adapter need not be default-constructible.
    //
    template <typename A>
    struct is_valid_adapter {
    private:
        template <typename T>
        static auto check(int) -> decltype(
            (void) static_cast<bool>(std::declval<T>().isOpen()),
            (void) static_cast<int>(std::declval<T>().available()),
            (void) static_cast<int>(std::declval<T>().read()),
            (void)std::declval<T>().write(uint8_t(0)),
            (void)std::declval<T>().write((const uint8_t*)0, size_t(0)),
            (void) static_cast<unsigned long>(std::declval<T>().millis()),
            std::true_type{}
            );

        template <typename>
        static std::false_type check(...);

    public:
        static constexpr bool value = decltype(check<A>(0))::value;
    };

    template <typename A>
    using adapter_check_t = typename std::enable_if<is_valid_adapter<A>::value, int>::type;

#else
    // -----------------------------------------------------------------------------
    //  AVR: No SFINAE validation (avr-gcc has no <type_traits>)
    // -----------------------------------------------------------------------------
    //
    // On AVR targets the adapter contract is trusted to be correct by the user.
    // The template parameter is preserved for API compatibility.
    //
    template <typename A>
    using adapter_check_t = int;

#endif

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
        uint16_t _overflowErrs = 0;

        ReceiverCallback _onReceive = nullptr;

        // Protocol callbacks
        typedef void (*AckReceivedCallback)(uint8_t seq);
        typedef void (*AckFailedCallback)(uint8_t seq, TinyResult reason);
        typedef void (*HandshakeFailedCallback)();
        typedef void (*StatusReceivedCallback)(const TinyStatusPayload& status);

        AckReceivedCallback     _onAckReceived = nullptr;
        AckFailedCallback       _onAckFailed = nullptr;
        HandshakeFailedCallback _onHandshakeFailed = nullptr;
        StatusReceivedCallback  _onStatusReceived = nullptr;

        static const size_t PLAIN_SIZE = 3 + sizeof(T) + 2;
        static const size_t PROTO_FRAME_MAX = 3 + sizeof(TinyStatusPayload) + 2; // 9 bytes
        static const size_t DECODE_BUF_SIZE = PLAIN_SIZE > PROTO_FRAME_MAX ? PLAIN_SIZE : PROTO_FRAME_MAX;

        uint8_t _pBuf[DECODE_BUF_SIZE];
        uint8_t _rawBuf[PLAIN_SIZE + 64];

        uint8_t _rawIdx = 0;
        uint8_t _currType = 0;
        uint8_t _currSeq = 0;
        uint8_t _nextSeq = 0;

        bool _hasNew = false;

        unsigned long _lastByte = 0;
        unsigned long _timeout = 250;

        // State machine
        TinyState _state = TinyState::CONNECTING;

        // Handshake configuration and state
        unsigned long _ackTimeout = 500;
        unsigned long _handshakeTimeout = 5000;
        uint8_t       _maxHandshakeRetries = 2;
        uint8_t       _handshakeRetryCount = 0;
        uint8_t       _statusFlags = 0x00;
        unsigned long _ackTimer = 0;
        unsigned long _handshakeTimer = 0;
        uint8_t       _lastSentSeq = 0;

        // RX resync flag (NEW)
        bool _inResync = false;

        // -------------------------------------------------------------------------
        // Internal: send a fixed-size protocol frame (ACK or STATUS)
        // -------------------------------------------------------------------------
        void _sendInternal(uint8_t type, uint8_t seq, const uint8_t* payload, uint8_t payloadLen) {
            const uint8_t FRAME_SIZE = 3 + payloadLen + 2;
            uint8_t buf[16];
            uint8_t enc[32];

            buf[0] = type;
            buf[1] = seq;
            buf[2] = payloadLen;
            memcpy(buf + 3, payload, payloadLen);

            uint16_t chk = fletcher16(buf, FRAME_SIZE - 2);
            buf[FRAME_SIZE - 2] = chk & 0xFF;
            buf[FRAME_SIZE - 1] = (chk >> 8) & 0xFF;

            size_t eLen = cobs_encode(buf, FRAME_SIZE, enc);
            _hw->write(0x00);
            _hw->write(enc, eLen);
            _hw->write(0x00);
        }

        // -------------------------------------------------------------------------
        // Internal: send a TYPE_STATUS frame (handshake / boot-time state exchange)
        // -------------------------------------------------------------------------
        void _sendStatus() {
            TinyStatusPayload sp;
            sp.state = static_cast<uint8_t>(_state);
            sp.lastSeq = _currSeq;
            sp.lastErr = TinyResult::OK;
            sp.flags = _statusFlags;

            uint8_t seq = _nextSeq++;
            _lastSentSeq = seq;
            _handshakeTimer = _hw->millis() | 1UL;

            _sendInternal(TYPE_STATUS, seq,
                reinterpret_cast<const uint8_t*>(&sp),
                static_cast<uint8_t>(sizeof(sp)));
        }

        // -------------------------------------------------------------------------
        // Internal: send a TYPE_ACK frame in response to a received packet
        // -------------------------------------------------------------------------
        void _sendAck(uint8_t ackedSeq, TinyResult result) {
            TinyAck ack;
            ack.seq = ackedSeq;
            ack.result = result;

            _sendInternal(TYPE_ACK, _nextSeq++,
                reinterpret_cast<const uint8_t*>(&ack),
                static_cast<uint8_t>(sizeof(ack)));
        }

        // -------------------------------------------------------------------------
        // Internal: process a fully decoded and validated frame
        // Returns true if the frame contains user-visible data (update() should return)
        // -------------------------------------------------------------------------
        bool _processFrame(uint8_t msgType, uint8_t msgSeq) {
            if (msgType == TYPE_STATUS) {
                TinyStatusPayload sp;
                memcpy(&sp, &_pBuf[3], sizeof(TinyStatusPayload));

                if (_state == TinyState::CONNECTING) {
                    _sendAck(msgSeq, TinyResult::OK);
                    _state = TinyState::HANDSHAKING;
                }
                else if (_state == TinyState::HANDSHAKING) {
                    // Duplicate STATUS — re-ACK it
                    _sendAck(msgSeq, TinyResult::OK);
                }

                if (_onStatusReceived) _onStatusReceived(sp);
                return false;

            }
            else if (msgType == TYPE_ACK) {
                TinyAck ack;
                memcpy(&ack, &_pBuf[3], sizeof(TinyAck));

                if (_state == TinyState::AWAITING_ACK && ack.seq == _lastSentSeq) {
                    if (_onAckReceived) _onAckReceived(ack.seq);
                    _state = TinyState::WAIT_FOR_SYNC;
                    _rawIdx = 0;
                }
                else if (_state == TinyState::HANDSHAKING && ack.seq == _lastSentSeq) {
                    // ACK for our STATUS → handshake complete
                    _state = TinyState::WAIT_FOR_SYNC;
                    _rawIdx = 0;
                }
                return false;

            }
            else {
                _currType = msgType;
                _currSeq = msgSeq;
                memcpy(&_data, &_pBuf[3], sizeof(T));

                _hasNew = true;
                _stats.packets++;

                if (msgType == TYPE_DATA || msgType == TYPE_COMMAND) {
                    _sendAck(msgSeq, TinyResult::OK);
                }

                if (_onReceive) _onReceive(_data);
                return true;
            }
        }

    public:

        TinyLink(Adapter& hw) : _hw(&hw) {
            static_assert(sizeof(T) <= 64, "TinyLink: Payload exceeds 64 bytes. TinyLink is designed for micro-messages.");
            static_assert(alignof(T) == 1, "TinyLink: Data type must be packed. Use __attribute__((packed)).");
        }

        void onReceive(ReceiverCallback cb) { _onReceive = cb; }
        void onAckReceived(AckReceivedCallback cb) { _onAckReceived = cb; }
        void onAckFailed(AckFailedCallback cb) { _onAckFailed = cb; }
        void onHandshakeFailed(HandshakeFailedCallback cb) { _onHandshakeFailed = cb; }
        void onStatusReceived(StatusReceivedCallback cb) { _onStatusReceived = cb; }

        void setTimeout(unsigned long ms) { _timeout = ms; }
        void setAckTimeout(unsigned long ms) { _ackTimeout = ms; }
        void setHandshakeTimeout(unsigned long ms) { _handshakeTimeout = ms; }
        void setMaxHandshakeRetries(uint8_t n) { _maxHandshakeRetries = n; }
        void setStatusFlags(uint8_t flags) { _statusFlags = flags; }

        void clearStats() { memset(&_stats, 0, sizeof(TinyStats)); }

        bool connected() {
            return _state != TinyState::CONNECTING && _state != TinyState::HANDSHAKING;
        }

        const TinyStats& getStats() { return _stats; }

        TinyState state() { return _state; }

        void reset() {
            _state = TinyState::CONNECTING;
            _rawIdx = 0;
            _handshakeTimer = 0;
            _handshakeRetryCount = 0;
            _ackTimer = 0;
            _hasNew = false;
        }

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
            if (!_hw->isOpen()) return;
            if (_hasNew) return;

            // ---- Handshake: initiate STATUS on first call ----
            if (_state == TinyState::CONNECTING) {
                if (_handshakeTimer == 0) {
                    _sendStatus();
                }
                else if (_hw->millis() - _handshakeTimer > _handshakeTimeout) {
                    if (_handshakeRetryCount < _maxHandshakeRetries) {
                        _handshakeRetryCount++;
                        _sendStatus();
                    }
                    else {
                        if (_onHandshakeFailed) _onHandshakeFailed();
                        _handshakeRetryCount = 0;
                        _handshakeTimer = _hw->millis() | 1UL;
                    }
                }
            }

            // ---- ACK timeout (only when no bytes are being accumulated) ----
            if (_state == TinyState::AWAITING_ACK && _rawIdx == 0) {
                if (_hw->millis() - _ackTimer > _ackTimeout) {
                    if (_onAckFailed) _onAckFailed(_lastSentSeq, TinyResult::ERR_TIMEOUT);
                    _state = TinyState::WAIT_FOR_SYNC;
                    _rawIdx = 0;
                }
            }

            // ---- IN_FRAME inter-byte timeout ----
            if (_state == TinyState::IN_FRAME && (_hw->millis() - _lastByte > _timeout)) {
                _state = TinyState::WAIT_FOR_SYNC;
                _rawIdx = 0;
                _stats.timeouts++;
            }

            while (_hw->available() > 0) {
                int incoming = _hw->read();
                if (incoming < 0) break;

                uint8_t c = (uint8_t)incoming;
                _lastByte = _hw->millis();

                // --- RX resync: discard everything until we see a delimiter
                if (_inResync) {
                    if (c == 0x00) {
                        _inResync = false;
                        _rawIdx = 0;
                    }
                    continue;
                }

                // -------------------------------------------------------------
                // Frame delimiter detected (COBS frame boundary)
                // -------------------------------------------------------------
                if (c == 0x00) {
                    if (_rawIdx >= 5) {
                        size_t dLen = cobs_decode(_rawBuf, _rawIdx, _pBuf);
                        _rawIdx = 0;
                        if (_state == TinyState::IN_FRAME) {
                            _state = TinyState::WAIT_FOR_SYNC;
                        }

                        if (dLen >= 3) {
                            uint8_t msgType = _pBuf[0];
                            uint8_t msgSeq = _pBuf[1];

                            // Expected decoded frame size depends on message type
                            size_t expLen;
                            if (msgType == TYPE_ACK) {
                                expLen = 3 + sizeof(TinyAck) + 2;
                            }
                            else if (msgType == TYPE_STATUS) {
                                expLen = 3 + sizeof(TinyStatusPayload) + 2;
                            }
                            else {
                                expLen = PLAIN_SIZE;
                            }

                            if (dLen == expLen) {
                                uint16_t calc = fletcher16(_pBuf, dLen - 2);
                                uint16_t recv =
                                    uint16_t(_pBuf[dLen - 2]) |
                                    (uint16_t(_pBuf[dLen - 1]) << 8);

                                if (calc == recv) {
                                    if (_processFrame(msgType, msgSeq)) {
                                        return;
                                    }
                                }
                                else {
                                    _stats.crcErrs++;
                                    _inResync = true;
                                }
                            }
                            else {
                                _stats.crcErrs++;
                                _inResync = true;
                            }
                        }
                    }
                    else {
                        _rawIdx = 0;
                        if (_state == TinyState::IN_FRAME) {
                            _state = TinyState::WAIT_FOR_SYNC;
                        }
                    }
                }
                // -------------------------------------------------------------
                // Accumulate bytes
                // -------------------------------------------------------------
                else {
                    if (_rawIdx < sizeof(_rawBuf)) {
                        _rawBuf[_rawIdx++] = c;
                        if (_state == TinyState::WAIT_FOR_SYNC) {
                            _state = TinyState::IN_FRAME;
                        }
                    }
                    else {
                        _overflowErrs++;
                        _rawIdx = 0;
                        if (_state == TinyState::IN_FRAME) {
                            _state = TinyState::WAIT_FOR_SYNC;
                        }
                        _inResync = true;
                    }
                }
            }
        }

        // -------------------------------------------------------------------------
        // TX: Encode + Send Frame
        // -------------------------------------------------------------------------
        void send(uint8_t type, const T& payload) {
            // Block during handshake — protocol messages take precedence
            if (_state == TinyState::CONNECTING || _state == TinyState::HANDSHAKING)
                return;

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

            // Enter AWAITING_ACK for reliable message types
            if (type == TYPE_DATA || type == TYPE_COMMAND) {
                _lastSentSeq = _currSeq;
                _ackTimer = _hw->millis();
                _state = TinyState::AWAITING_ACK;
            }
        }
    };

} // namespace tinylink

#endif // TINY_LINK_H
