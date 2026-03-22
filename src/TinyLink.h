/**
 * @file TinyLink.h
 * @brief Main template engine for the TinyLink protocol.
 */

#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "TinyProtocol.h"
#include "CobsCodec.h"
#include "Fletcher16.h"
#include "Packet.h"
#include "version.h"
#include "protocol/internal/AckMessage.h"
#include "protocol/internal/HandshakeMessage.h"
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
        // Member Variables
        // -------------------------------------------------------------------------
        Adapter* _hw;
        T _data;

        TinyStats _stats = TinyStats{};

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

        TinyState _state = TinyState::WAIT_FOR_SYNC;

        // -------------------------------------------------------------------------
        // Internal Sender
        // -------------------------------------------------------------------------
        // Packs, COBS-encodes and writes a frame without touching public sequencing
        // (_nextSeq / _currSeq) or stats when internal=true.
        // Returns true if the frame was handed off to the hardware layer.
        bool send_internal(uint8_t wireType, uint8_t seq, const uint8_t *payload, size_t len, bool internal) {
            if (!_hw->isOpen()) return false;

            // Use a local buffer sized for the largest internal payload (TinyAck = 2 bytes).
            // Plain packet layout: [type(1)][seq(1)][len(1)][payload][crc_lo(1)][crc_hi(1)]
            // COBS worst-case overhead: ceil(n/254)+1 extra bytes. For n=7: 2 bytes max.
            // +4 is a conservative upper bound that keeps the buffer safely oversized.
            const size_t MAX_INTERNAL_PLAIN = 3 + sizeof(TinyAck) + 2; // 7 bytes
            const size_t MAX_INTERNAL_ENC   = MAX_INTERNAL_PLAIN + 4;  // 11 bytes (conservative COBS upper bound)
            uint8_t pBuf[MAX_INTERNAL_PLAIN];
            uint8_t rawBuf[MAX_INTERNAL_ENC];

            size_t plainLen = tinylink::packet::pack(wireType, seq, payload, len, pBuf, MAX_INTERNAL_PLAIN);
            if (plainLen == 0) {
                if (!internal) _stats.increment(TinyStatus::ERR_CRC);
                return false;
            }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen, rawBuf, MAX_INTERNAL_ENC);

            _hw->write(0x00);
            _hw->write(rawBuf, eLen);
            _hw->write(0x00);

            _rawIdx = 0;
            return true;
        }

        // -------------------------------------------------------------------------
        // Handshake Helpers
        // -------------------------------------------------------------------------

        // Send a Handshake frame and transition to CONNECTING state.
        void startHandshake() {
            HandshakeMessage m;
            m.version = 0;
            send_internal(message_type_to_wire(MessageType::Handshake),
                          0, reinterpret_cast<const uint8_t*>(&m), sizeof(m), true);
            _state = TinyState::CONNECTING;
        }

        // Handle an incoming Handshake frame: transition state and reply with a TinyAck.
        void handleHandshakeMessage(const uint8_t *payload, size_t len) {
            if (len < sizeof(HandshakeMessage)) return;

            HandshakeMessage h;
            memcpy(&h, payload, sizeof(h));

            if (_state != TinyState::WAIT_FOR_SYNC) {
                _state = TinyState::HANDSHAKING;
            }

            TinyAck ack;
            ack.seq    = 0;
            ack.result = TinyStatus::STATUS_OK;
            send_internal(message_type_to_wire(MessageType::Ack),
                          0, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), true);
        }

    public:

        // -------------------------------------------------------------------------
        // Constructor
        // -------------------------------------------------------------------------
        TinyLink(Adapter& hw) : _hw(&hw) {
            static_assert(sizeof(T) <= 64, "TinyLink: Payload exceeds 64 bytes. TinyLink is designed for micro-messages.");
            static_assert(alignof(T) == 1, "TinyLink: Data type must be packed. Use __attribute__((packed)).");
        }

        void onReceive(ReceiverCallback cb) { _onReceive = cb; }
        void setTimeout(unsigned long ms) { _timeout = ms; }
        void clearStats() { _stats.clear(); }

        bool connected() { return _hw->isOpen(); }
        const TinyStats& getStats() { return _stats; }

        bool available() { return _hasNew; }
        const T& peek() { return _data; }
        void flush() { _hasNew = false; }

        uint8_t type() { return _currType; }
        uint8_t seq() { return _currSeq; }

        uint16_t overflowErrors() const { return static_cast<uint16_t>(_stats.overflow); }


        // -------------------------------------------------------------------------
        // Update: Main RX State Machine
        // -------------------------------------------------------------------------
        void update() {
            if (!_hw->isOpen() || _hasNew)
                return;

            if (_rawIdx > 0 && (_hw->millis() - _lastByte > _timeout)) {
                _rawIdx = 0;
                _stats.increment(TinyStatus::ERR_TIMEOUT);
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
                        size_t dLen = tinylink::codec::cobs_decode(_rawBuf, _rawIdx, _pBuf, sizeof(_pBuf));

                        // ---------------------------------------------------------
                        // Internal dispatch: Handshake and handshake-confirmation ACK
                        // These frames are consumed before the public handler so they
                        // do not affect public stats or the user-visible data stream.
                        // ---------------------------------------------------------
                        if (dLen >= 6) {
                            uint8_t wireType = _pBuf[0];

                            // Handshake frame: handle internally and reply with ACK.
                            if (wireType == message_type_to_wire(MessageType::Handshake)) {
                                uint8_t hsBuf[sizeof(HandshakeMessage)];
                                uint8_t rtype = 0, rseq = 0;
                                size_t hsLen = tinylink::packet::unpack(_pBuf, dLen, &rtype, &rseq,
                                                                        hsBuf, sizeof(hsBuf));
                                if (hsLen == sizeof(HandshakeMessage)) {
                                    handleHandshakeMessage(hsBuf, hsLen);
                                }
                                _rawIdx = 0;
                                continue;
                            }

                            // Handshake-confirmation ACK: consume if we are in CONNECTING state.
                            if (wireType == message_type_to_wire(MessageType::Ack)
                                    && _state == TinyState::CONNECTING) {
                                uint8_t ackBuf[sizeof(TinyAck)];
                                uint8_t rtype = 0, rseq = 0;
                                size_t ackLen = tinylink::packet::unpack(_pBuf, dLen, &rtype, &rseq,
                                                                         ackBuf, sizeof(ackBuf));
                                if (ackLen == sizeof(TinyAck)) {
                                    TinyAck ack;
                                    memcpy(&ack, ackBuf, sizeof(ack));
                                    if (ack.result == TinyStatus::STATUS_OK) {
                                        _state = TinyState::WAIT_FOR_SYNC;
                                        _rawIdx = 0;
                                        continue;
                                    }
                                }
                            }
                        }

                        {
                            uint8_t rtype = 0, rseq = 0;
                            size_t payloadLen = tinylink::packet::unpack(_pBuf, dLen, &rtype, &rseq, (uint8_t*)&_data, sizeof(T));
                            if (payloadLen == sizeof(T)) {
                                _currType = rtype;
                                _currSeq = rseq;
                                _stats.increment(TinyStatus::STATUS_OK);
                                _rawIdx = 0;
                                if (_onReceive) { _onReceive(_data); } else { _hasNew = true; }
                                return;
                            } else {
                                _stats.increment(TinyStatus::ERR_CRC);
                            }
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
                        _stats.increment(TinyStatus::ERR_OVERFLOW);
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
            size_t plainLen = tinylink::packet::pack(type, _currSeq, (const uint8_t*)&payload, sizeof(T), _pBuf, sizeof(_pBuf));
            if (plainLen == 0) { _stats.increment(TinyStatus::ERR_CRC); return; }
            size_t eLen = tinylink::codec::cobs_encode(_pBuf, plainLen, _rawBuf, sizeof(_rawBuf));

            _hw->write(0x00);
            _hw->write(_rawBuf, eLen);
            _hw->write(0x00);

            _rawIdx = 0;
        }
    };

} // namespace tinylink

#endif // TINY_LINK_H
