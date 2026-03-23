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
#include "protocol/internal/DebugMessage.h"
#include "protocol/internal/HandshakeMessage.h"
#include "protocol/LogLevel.h"
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
        // Buffer sizing
        //
        // _pBuf must be large enough to hold the decoded plain-text of both user
        // frames (3 + sizeof(T) + 2) and internal protocol frames (TinyAck,
        // HandshakeMessage).  We take the maximum across all three so that the
        // same buffer can safely receive any frame type regardless of T's size.
        // -------------------------------------------------------------------------
        static const size_t INTERNAL_PAYLOAD_SIZE =
            sizeof(TinyAck) > sizeof(HandshakeMessage)
                ? sizeof(TinyAck)
                : sizeof(HandshakeMessage);

        static const size_t MAX_PAYLOAD_SIZE =
            sizeof(T) > INTERNAL_PAYLOAD_SIZE
                ? sizeof(T)
                : INTERNAL_PAYLOAD_SIZE;

        static const size_t PLAIN_SIZE = 3 + MAX_PAYLOAD_SIZE + 2;

        // -------------------------------------------------------------------------
        // Member Variables
        // -------------------------------------------------------------------------
        Adapter* _hw;
        T _data;

        TinyStats _stats;

        ReceiverCallback _onReceive;

        uint8_t _pBuf[PLAIN_SIZE];
        // RX accumulation buffer: fixed at the protocol maximum frame size (T=64 → 73 bytes)
        // so that structurally mismatched frames from peers with larger payloads are received
        // in full and rejected by the size/checksum check rather than by a premature overflow.
        static const size_t MAX_FRAME_PLAIN = 3 + 64 + 2;         // 69 bytes
        static const size_t MAX_FRAME_ENC   = MAX_FRAME_PLAIN + 4; // 73 bytes (COBS overhead)
        uint8_t _rawBuf[MAX_FRAME_ENC];

        uint16_t _rawIdx;
        uint8_t  _currType;
        uint8_t  _currSeq;
        uint8_t  _nextSeq;

        bool _hasNew;

        unsigned long _lastByte;
        unsigned long _timeout;

        TinyState _state;

        // -------------------------------------------------------------------------
        // Non-copyable
        // -------------------------------------------------------------------------
        TinyLink(const TinyLink&) = delete;
        TinyLink& operator=(const TinyLink&) = delete;

        // -------------------------------------------------------------------------
        // Internal Sender
        // -------------------------------------------------------------------------
        // Packs, COBS-encodes and writes a frame using local stack buffers.
        // Uses local (stack) buffers so it never aliases the RX accumulation
        // buffers (_pBuf / _rawBuf), making full-duplex operation safe.
        // Returns true if the frame was handed off to the hardware layer.
        bool send_internal(uint8_t wireType, uint8_t seq, const uint8_t *payload, size_t len, bool internal) {
            if (!_hw->isOpen()) return false;

            // Plain packet layout: [type(1)][seq(1)][len(1)][payload(0-64)][crc_lo(1)][crc_hi(1)]
            // COBS worst-case overhead: ceil(n/254)+1 extra bytes. For n=69: 2 bytes.
            // +4 is a conservative upper bound that keeps the buffer safely oversized.
            const size_t MAX_INTERNAL_PLAIN = 3 + 64 + 2; // 69 bytes (max payload 64 bytes)
            const size_t MAX_INTERNAL_ENC   = MAX_INTERNAL_PLAIN + 4;  // 73 bytes
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

            return true;
        }

        // -------------------------------------------------------------------------
        // Handshake Helpers
        // -------------------------------------------------------------------------

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

        // -------------------------------------------------------------------------
        // Private sendLog overload (raw level byte — used internally)
        // -------------------------------------------------------------------------
        bool sendLog(uint8_t level, uint16_t code, const char *text) {
            return sendLog(static_cast<LogLevel>(level), code, text);
        }

    public:

        // -------------------------------------------------------------------------
        // Constructor
        // -------------------------------------------------------------------------
        explicit TinyLink(Adapter& hw)
            : _hw(&hw),
              _data(),
              _stats(),
              _onReceive(nullptr),
              _rawIdx(0),
              _currType(0),
              _currSeq(0),
              _nextSeq(0),
              _hasNew(false),
              _lastByte(0),
              _timeout(250),
              _state(TinyState::WAIT_FOR_SYNC)
        {
            static_assert(sizeof(T) <= 64,
                "TinyLink: Payload exceeds 64 bytes. TinyLink is designed for micro-messages.");
            static_assert(alignof(T) == 1,
                "TinyLink: Data type must be packed. Use __attribute__((packed)).");
        }

        // -------------------------------------------------------------------------
        // Configuration & Control
        // -------------------------------------------------------------------------

        void onReceive(ReceiverCallback cb) { _onReceive = cb; }
        void setTimeout(unsigned long ms)   { _timeout = ms; }
        void clearStats()                   { _stats.clear(); }

        /**
         * @brief Initiate a connection handshake with the remote peer.
         *
         * Sends a Handshake ('H') frame and transitions to CONNECTING state.
         * The peer should reply with an ACK, which drives the state to HANDSHAKING.
         * Data exchange can proceed regardless of handshake state; the handshake
         * is informational only and does not gate the RX path.
         */
        void startHandshake() {
            HandshakeMessage m;
            m.version = 0;
            send_internal(message_type_to_wire(MessageType::Handshake),
                          0, reinterpret_cast<const uint8_t*>(&m), sizeof(m), true);
            _state = TinyState::CONNECTING;
        }

        // -------------------------------------------------------------------------
        // Status / Telemetry
        // -------------------------------------------------------------------------

        /** @brief Returns true if the underlying hardware port is open. */
        bool connected() const { return _hw->isOpen(); }

        /** @brief Returns the current connection/handshake state. */
        TinyState state() const { return _state; }

        /** @brief Returns accumulated protocol statistics (read-only). */
        const TinyStats& getStats() const { return _stats; }

        // -------------------------------------------------------------------------
        // Polling API
        // -------------------------------------------------------------------------

        /** @brief Returns true if a new packet has been received and not yet consumed. */
        bool          available() const { return _hasNew; }
        /** @brief Returns a const reference to the last received payload (zero-copy). */
        const T&      peek()      const { return _data; }
        /** @brief Clears the available flag so the next packet can be received. */
        void          flush()           { _hasNew = false; }

        /** @brief Returns the wire message type byte of the last received frame. */
        uint8_t type() const { return _currType; }
        /** @brief Returns the sequence number of the last received frame. */
        uint8_t seq()  const { return _currSeq; }

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
        //
        // Uses local stack buffers so it never aliases the RX accumulation buffers
        // (_pBuf / _rawBuf), making full-duplex use safe.
        // -------------------------------------------------------------------------
        void sendData(uint8_t type, const T& payload) {
            if (!_hw->isOpen()) return;

            // Local buffers for TX encoding — do not touch the class-member RX buffers.
            const size_t TX_PLAIN = 3 + sizeof(T) + 2;
            const size_t TX_ENC   = TX_PLAIN + 4;
            uint8_t pBuf[TX_PLAIN];
            uint8_t rawBuf[TX_ENC];

            _currSeq = _nextSeq++;
            size_t plainLen = tinylink::packet::pack(type, _currSeq, (const uint8_t*)&payload, sizeof(T), pBuf, TX_PLAIN);
            if (plainLen == 0) { _stats.increment(TinyStatus::ERR_CRC); return; }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen, rawBuf, TX_ENC);

            _hw->write(0x00);
            _hw->write(rawBuf, eLen);
            _hw->write(0x00);
        }

        // -------------------------------------------------------------------------
        // Logging API
        //
        // Sends a structured Debug ('g') frame using the DebugMessage wire format.
        // The receiver can cast the payload directly to a DebugMessage struct.
        //
        // Wire layout: see DebugMessage — [ts:4][level:1][code:2][text:25]
        // Text is truncated to DEBUG_TEXT_CAPACITY - 1 (24) characters.
        // -------------------------------------------------------------------------
        bool sendLog(LogLevel level, uint16_t code, const char *text) {
            if (!_hw->isOpen()) return false;

            DebugMessage msg;
            msg.ts    = static_cast<uint32_t>(_hw->millis());
            msg.level = static_cast<uint8_t>(level);
            msg.code  = code;
            debugmessage_set_text(msg, text);

            _currSeq = _nextSeq++;
            return send_internal(message_type_to_wire(MessageType::Debug),
                                 _currSeq, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg),
                                 /*internal=*/false);
        }
    };

} // namespace tinylink

#endif // TINY_LINK_H
