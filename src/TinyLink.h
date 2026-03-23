/**
 * @file TinyLink.h
 * @brief Main template engine for the TinyLink protocol.
 */

#ifndef TINY_LINK_H
#define TINY_LINK_H

#include "protocol/MessageType.h"
#include "protocol/Status.h"
#include "protocol/State.h"
#include "protocol/Stats.h"
#include "internal/codec/CobsCodec.h"
#include "internal/codec/Fletcher16.h"
#include "internal/codec/Packet.h"
#include "Version.h"
#include "internal/protocol/AckMessage.h"
#include "internal/protocol/LogMessage.h"
#include "internal/protocol/HandshakeMessage.h"
#include "protocol/LogLevel.h"
#include <stdint.h>
#include <string.h>

#ifndef __AVR__
#include <type_traits>
#endif

namespace tinylink {

#ifndef __AVR__
    // -------------------------------------------------------------------------
    //  SFINAE Adapter Validation (C++11/14, non-AVR only)
    // -------------------------------------------------------------------------
    template <typename A>
    struct is_valid_adapter {
    private:
        template <typename U>
        static auto check(int) -> decltype(
            (void) static_cast<bool>(std::declval<U>().isOpen()),
            (void) static_cast<int>(std::declval<U>().available()),
            (void) static_cast<int>(std::declval<U>().read()),
            (void)std::declval<U>().write(uint8_t(0)),
            (void)std::declval<U>().write((const uint8_t*)0, size_t(0)),
            (void) static_cast<unsigned long>(std::declval<U>().millis()),
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
    template <typename A>
    using adapter_check_t = int;
#endif

    // =========================================================================
    //  TinyLink Engine
    // =========================================================================

    template <typename T, typename Adapter, adapter_check_t<Adapter> = 0>
    class TinyLink {
    public:
        typedef void (*DataCallback)(const T& data);
        typedef void (*LogCallback)(const LogMessage& msg);
        typedef void (*HandshakeCallback)(const HandshakeMessage& msg);
        typedef void (*AckCallback)(const TinyAck& ack);

    private:

        // ---- Buffer sizing --------------------------------------------------
        //
        // _pBuf must hold the largest plain-text frame we will ever decode:
        //   user data    : 3 + sizeof(T) + 2
        //   internal ACK : 3 + sizeof(TinyAck=2) + 2  = 7
        //   Handshake    : 3 + sizeof(HandshakeMessage=1) + 2 = 6
        //   Log          : 3 + sizeof(LogMessage=32) + 2 = 37
        //
        static const size_t INTERNAL_PAYLOAD_ACK_HS =
            sizeof(TinyAck) > sizeof(HandshakeMessage)
                ? sizeof(TinyAck) : sizeof(HandshakeMessage);
        static const size_t INTERNAL_PAYLOAD_SIZE =
            INTERNAL_PAYLOAD_ACK_HS > sizeof(LogMessage)
                ? INTERNAL_PAYLOAD_ACK_HS : sizeof(LogMessage);
        static const size_t MAX_PAYLOAD_SIZE =
            sizeof(T) > INTERNAL_PAYLOAD_SIZE
                ? sizeof(T) : INTERNAL_PAYLOAD_SIZE;
        static const size_t PLAIN_SIZE = 3 + MAX_PAYLOAD_SIZE + 2;

        // RX accumulation buffer: fixed at the protocol maximum (T=64 → 73 B)
        // so mismatched-size frames are rejected by CRC, not premature overflow.
        static const size_t MAX_FRAME_PLAIN = 3 + 64 + 2;          // 69 B
        static const size_t MAX_FRAME_ENC   = MAX_FRAME_PLAIN + 4;  // 73 B

        // Interval between Handshake retries while in CONNECTING.
        static const unsigned long HANDSHAKE_RETRY_MS = 1000;

        // ---- Members --------------------------------------------------------
        Adapter*          _hw;
        T                 _data;
        TinyStats         _stats;
        DataCallback      _onDataReceived;
        LogCallback       _onLogReceived;
        HandshakeCallback _onHandshakeReceived;
        AckCallback       _onAckReceived;

        static TinyLink*  _s_instance; ///< For enableAutoUpdate() / autoUpdateISR()

        uint8_t  _pBuf[PLAIN_SIZE];
        uint8_t  _rawBuf[MAX_FRAME_ENC];
        uint16_t _rawIdx;

        uint8_t       _currType;
        uint8_t       _currSeq;
        uint8_t       _nextSeq;
        bool          _hasNew;
        unsigned long _lastByte;
        unsigned long _timeout;

        TinyState     _state;
        bool          _handshakeEnabled;
        unsigned long _lastHandshakeSent;
        unsigned long _lastSent;          // timestamp of last sendData(), for AWAITING_ACK timeout

        // ---- Non-copyable ---------------------------------------------------
        TinyLink(const TinyLink&) = delete;
        TinyLink& operator=(const TinyLink&) = delete;

        // ---- Internal frame sender ------------------------------------------
        //
        // Uses local (stack) buffers — never aliases the RX buffers, making
        // full-duplex operation safe.
        //
        bool send_internal(uint8_t wireType, uint8_t seq,
                           const uint8_t* payload, size_t len, bool isInternal) {
            if (!_hw->isOpen()) return false;

            const size_t MAX_PLAIN = 3 + 64 + 2;
            const size_t MAX_ENC   = MAX_PLAIN + 4;
            uint8_t pBuf[MAX_PLAIN];
            uint8_t rawBuf[MAX_ENC];

            size_t plainLen = tinylink::packet::pack(wireType, seq, payload, len,
                                                     pBuf, MAX_PLAIN);
            if (plainLen == 0) {
                if (!isInternal) _stats.increment(TinyStatus::ERR_CRC);
                return false;
            }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen,
                                                       rawBuf, MAX_ENC);
            _hw->write(0x00);
            _hw->write(rawBuf, eLen);
            _hw->write(0x00);
            return true;
        }

        // ---- Handshake handler (symmetric two-frame exchange) ---------------
        //
        // The HandshakeMessage::version byte distinguishes the two frame kinds:
        //   version = 0  →  initial HS  (connection request, sent by begin())
        //   version = 1  →  HS reply    (acknowledgement, sent in response)
        //
        // Rules:
        //   Receive HS(v=0) from peer
        //     → always send HS(v=1) back (helps peer connect / reconnect)
        //     → if we were CONNECTING, the reply from us signals readiness
        //        and we advance to WAIT_FOR_SYNC
        //
        //   Receive HS(v=1) from peer
        //     → if we were CONNECTING, peer acknowledged our initial HS
        //        → advance to WAIT_FOR_SYNC (no further reply needed)
        //     → if already connected, ignore (stale reply, no ping-pong)
        //
        // This eliminates the need for a HANDSHAKING intermediate state.
        //
        // handleHandshakeMessage receives priorState (the state before we
        // entered FRAME_COMPLETE) so it can make correct transitions
        // regardless of the fact that _state is FRAME_COMPLETE at call time.
        void handleHandshakeMessage(const uint8_t* payload, size_t len,
                                    TinyState fromState) {
            if (len < sizeof(HandshakeMessage)) return;

            HandshakeMessage m;
            memcpy(&m, payload, sizeof(m));

            if (m.version == 0) {
                // Initial connection request: reply with HS(v=1).
                HandshakeMessage reply;
                reply.version = 1;
                send_internal(message_type_to_wire(MessageType::Handshake), 0,
                              reinterpret_cast<const uint8_t*>(&reply),
                              sizeof(reply), /*isInternal=*/true);

                // If we were waiting to connect, the reply signals readiness.
                if (fromState == TinyState::CONNECTING) {
                    _state = TinyState::WAIT_FOR_SYNC;
                } else {
                    // Peer joined or rebooted — restore appropriate idle state.
                    _state = (fromState == TinyState::AWAITING_ACK)
                             ? TinyState::AWAITING_ACK
                             : TinyState::WAIT_FOR_SYNC;
                }

            } else {
                // HS reply (v=1): peer acknowledged our begin().
                if (fromState == TinyState::CONNECTING) {
                    _state = TinyState::WAIT_FOR_SYNC;
                } else {
                    // Already connected — ignore to prevent ping-pong.
                    _state = (fromState == TinyState::AWAITING_ACK)
                             ? TinyState::AWAITING_ACK
                             : TinyState::WAIT_FOR_SYNC;
                }
            }
        }

        // Private raw-level sendLog (delegates to the typed overload).
        bool sendLog(uint8_t level, uint16_t code, const char* text) {
            return sendLog(static_cast<LogLevel>(level), code, text);
        }

    public:

        // ---- Constructor ----------------------------------------------------
        explicit TinyLink(Adapter& hw)
            : _hw(&hw), _data(), _stats(),
              _onDataReceived(nullptr), _onLogReceived(nullptr),
              _onHandshakeReceived(nullptr), _onAckReceived(nullptr),
              _rawIdx(0), _currType(0), _currSeq(0), _nextSeq(0),
              _hasNew(false), _lastByte(0), _timeout(250),
              _state(TinyState::WAIT_FOR_SYNC),
              _handshakeEnabled(false), _lastHandshakeSent(0), _lastSent(0)
        {
            static_assert(sizeof(T) <= 64,
                "TinyLink: Payload exceeds 64 bytes. "
                "TinyLink is designed for micro-messages.");
            static_assert(alignof(T) == 1,
                "TinyLink: Data type must be packed. "
                "Use __attribute__((packed)).");
            // Auto-register this instance so autoUpdateISR() works without
            // any extra setup call.  Only one instance per <T,Adapter> pair
            // can be registered at a time; a later call to enableAutoUpdate()
            // can explicitly switch which instance is ISR-driven.
            _s_instance = this;
        }

        // ---- Configuration --------------------------------------------------

        /** @brief Register a callback for incoming user data frames. */
        void onDataReceived(DataCallback cb)           { _onDataReceived = cb; }
        /** @brief Register a callback for incoming log frames. */
        void onLogReceived(LogCallback cb)             { _onLogReceived = cb; }
        /** @brief Register a callback for incoming handshake frames. */
        void onHandshakeReceived(HandshakeCallback cb) { _onHandshakeReceived = cb; }
        /** @brief Register a callback for incoming ACK frames. */
        void onAckReceived(AckCallback cb)             { _onAckReceived = cb; }

        void setTimeout(unsigned long ms)              { _timeout = ms; }

        /**
         * @brief Explicitly select this instance for interrupt-driven updates.
         *
         * The most recently constructed TinyLink instance is automatically
         * registered, so this call is **optional** for the common single-instance
         * case.  Call it explicitly only when you have multiple TinyLink instances
         * of the same <T,Adapter> type and want to control which one receives ISR
         * calls.
         *
         * After calling (or without calling, for the default instance),
         * autoUpdateISR() can be passed to any hardware timer, UART RX interrupt,
         * or platform-specific interrupt-attachment routine.
         *
         * Example — explicit override for a second instance (Arduino + TimerOne):
         * @code
         *   TinyLink<MyData, MyAdapter> linkA(adapterA);
         *   TinyLink<MyData, MyAdapter> linkB(adapterB);
         *   // linkB was constructed last; autoUpdateISR() targets it by default.
         *   // To drive linkA from the ISR instead:
         *   linkA.enableAutoUpdate();
         *   Timer1.attachInterrupt(TinyLink<MyData, MyAdapter>::autoUpdateISR, 1000);
         * @endcode
         *
         * @note Only one TinyLink instance per T+Adapter type combination can be
         *       registered at a time. A second call overwrites the previous one.
         */
        void enableAutoUpdate() { _s_instance = this; }

        /**
         * @brief Static ISR-compatible update function.
         *
         * Targets the most recently constructed (or last enableAutoUpdate()-ed)
         * instance.  No prior setup call is required for the common case of one
         * TinyLink instance per <T,Adapter> type — construction is sufficient.
         *
         * Attach to any interrupt source that suits your platform:
         *   - A hardware timer (e.g., TimerOne, ESP32 hw_timer)
         *   - A UART RX interrupt
         *   - Arduino's serialEvent() for a zero-interrupt fallback
         *   - Any RTOS task / periodic callback
         *
         * Example — Arduino main-loop polling (no interrupt needed):
         * @code
         *   void loop() { link.update(); }          // works on every device
         * @endcode
         *
         * Example — Timer ISR (when hardware timer is available):
         * @code
         *   Timer1.attachInterrupt(TinyLink<MyData, MyAdapter>::autoUpdateISR, 1000);
         * @endcode
         *
         * Example — Arduino serialEvent() (no true interrupt required):
         * @code
         *   void serialEvent() {
         *       TinyLink<MyData, MyAdapter>::autoUpdateISR();
         *   }
         * @endcode
         *
         * @see enableAutoUpdate()
         */
        static void autoUpdateISR() {
            if (_s_instance) _s_instance->update();
        }

        /**
         * @brief Initiate the link and start the handshake exchange.
         *
         * Sends an initial Handshake frame (version=0) and transitions to
         * CONNECTING.  While CONNECTING the engine periodically re-sends the
         * Handshake (every HANDSHAKE_RETRY_MS = 1 s) until the peer replies.
         *
         * Handshake exchange (two frames, no intermediate state):
         *   1. begin()  →  sends HS(v=0)   →  state = CONNECTING
         *   2. Peer receives HS(v=0)  →  sends HS(v=1)  →  peer = WAIT_FOR_SYNC
         *   3. We receive HS(v=1)     →  state = WAIT_FOR_SYNC  (link ready)
         *
         * If both sides call begin() concurrently (common embedded scenario):
         *   Each side receives the other's HS(v=0), replies with HS(v=1),
         *   and advances to WAIT_FOR_SYNC — both connected in two round-trips.
         *
         * Data frames (sendData) are blocked until connected() returns true.
         * Calling begin() again re-initiates the handshake from CONNECTING.
         */
        void begin() {
            if (!_hw->isOpen()) return;
            _handshakeEnabled = true;
            _state            = TinyState::CONNECTING;
            _lastHandshakeSent = _hw->millis();
            HandshakeMessage m;
            m.version = 0;
            send_internal(message_type_to_wire(MessageType::Handshake), 0,
                          reinterpret_cast<const uint8_t*>(&m), sizeof(m),
                          /*isInternal=*/true);
        }

        /**
         * @brief Reset the protocol engine to its default state.
         *
         * Clears all counters, flags, and state without touching the hardware
         * adapter.  Does NOT re-initiate the handshake — call begin() for that.
         */
        void reset() {
            _handshakeEnabled  = false;
            _state             = TinyState::WAIT_FOR_SYNC;
            _rawIdx            = 0;
            _hasNew            = false;
            _currType          = 0;
            _currSeq           = 0;
            _nextSeq           = 0;
            _lastByte          = 0;
            _lastHandshakeSent = 0;
            _lastSent          = 0;
            _stats.clear();
            _onDataReceived    = nullptr;
            _onLogReceived     = nullptr;
            _onHandshakeReceived = nullptr;
            _onAckReceived     = nullptr;
        }

        /** @brief Clears all protocol statistics counters. */
        void clearStats() { _stats.clear(); }

        // ---- Status ---------------------------------------------------------

        /**
         * @brief True when the link is ready to exchange data frames.
         *
         * Basic mode (begin() never called): true as long as the hardware
         * port is open.
         *
         * Handshake mode (after begin()): true only once CONNECTING has
         * resolved to WAIT_FOR_SYNC or a data-phase state.
         */
        bool connected() const {
            if (!_hw->isOpen()) return false;
            if (!_handshakeEnabled) return true;
            return _state != TinyState::CONNECTING;
        }

        /** @brief Current state machine stage. */
        TinyState        state()    const { return _state; }

        /** @brief Accumulated protocol statistics (read-only). */
        const TinyStats& getStats() const { return _stats; }

        // ---- Polling API ----------------------------------------------------

        /** @brief True when a user data frame has been received and not consumed. */
        bool     available() const { return _hasNew; }
        /** @brief Zero-copy access to the last received payload. */
        const T& peek()      const { return _data; }
        /** @brief Clears the 'available' flag; call after reading peek(). */
        void     flush()           { _hasNew = false; }

        /** @brief Wire message-type byte of the most recently received frame. */
        uint8_t type() const { return _currType; }
        /** @brief Sequence number of the most recently received frame. */
        uint8_t seq()  const { return _currSeq; }

        // ---- update() — Main RX / State-Machine Driver ----------------------
        //
        // Call as often as possible from the main loop (or a timer ISR-safe
        // context).  Reads all bytes available from the adapter, drives the
        // RX state machine, and dispatches complete frames.
        //
        void update() {
            if (!_hw->isOpen() || _hasNew) return;

            unsigned long now = _hw->millis();

            // Periodic Handshake re-send while awaiting the peer.
            if (_handshakeEnabled && _state == TinyState::CONNECTING) {
                if (now - _lastHandshakeSent >= HANDSHAKE_RETRY_MS) {
                    _lastHandshakeSent = now;
                    HandshakeMessage m; m.version = 0;
                    send_internal(message_type_to_wire(MessageType::Handshake),
                                  0, reinterpret_cast<const uint8_t*>(&m),
                                  sizeof(m), true);
                }
            }

            // Inter-byte timeout: discard a stalled partial frame.
            if (_rawIdx > 0 && (now - _lastByte > _timeout)) {
                _rawIdx = 0;
                _stats.increment(TinyStatus::ERR_TIMEOUT);
                if (_state == TinyState::IN_FRAME) {
                    _state = TinyState::WAIT_FOR_SYNC;
                }
                // CONNECTING / AWAITING_ACK: partial frame discarded, but the
                // connection-phase state is preserved.
            }

            // ACK timeout: no Ack received within the allowed window.
            if (_handshakeEnabled &&
                    _state == TinyState::AWAITING_ACK && _rawIdx == 0) {
                if (now - _lastSent > _timeout) {
                    _stats.increment(TinyStatus::ERR_TIMEOUT);
                    _state = TinyState::WAIT_FOR_SYNC;
                }
            }

            // -----------------------------------------------------------------
            // Byte-level receive loop
            // -----------------------------------------------------------------
            while (_hw->available() > 0) {
                int incoming = _hw->read();
                if (incoming < 0) break;

                uint8_t c = static_cast<uint8_t>(incoming);
                _lastByte = _hw->millis();

                // ----------------------------------------------------------
                // 0x00 — COBS frame delimiter
                // ----------------------------------------------------------
                if (c == 0x00) {
                    if (_rawIdx >= 5) {
                        TinyState priorState = _state;
                        _state = TinyState::FRAME_COMPLETE;

                        size_t dLen = tinylink::codec::cobs_decode(
                            _rawBuf, _rawIdx, _pBuf, sizeof(_pBuf));
                        _rawIdx = 0;

                        // ---- Internal protocol frames ----
                        if (dLen >= 6) {
                            uint8_t wireType = _pBuf[0];

                            // Handshake: always handled internally.
                            if (wireType ==
                                    message_type_to_wire(MessageType::Handshake)) {
                                uint8_t hsBuf[sizeof(HandshakeMessage)];
                                uint8_t rtype = 0, rseq = 0;
                                size_t hsLen = tinylink::packet::unpack(
                                    _pBuf, dLen, &rtype, &rseq,
                                    hsBuf, sizeof(hsBuf));
                                if (hsLen == sizeof(HandshakeMessage)) {
                                    handleHandshakeMessage(hsBuf, hsLen,
                                                           priorState);
                                    if (_onHandshakeReceived) {
                                        HandshakeMessage hm;
                                        memcpy(&hm, hsBuf, sizeof(hm));
                                        _onHandshakeReceived(hm);
                                    }
                                } else {
                                    // Malformed HS — restore to idle state.
                                    _state = (priorState == TinyState::CONNECTING ||
                                              priorState == TinyState::AWAITING_ACK)
                                             ? priorState
                                             : TinyState::WAIT_FOR_SYNC;
                                }
                                continue;
                            }

                            // Ack: consumed when we are AWAITING_ACK.
                            if (wireType ==
                                    message_type_to_wire(MessageType::Ack) &&
                                    priorState == TinyState::AWAITING_ACK) {
                                uint8_t ackBuf[sizeof(TinyAck)];
                                uint8_t rtype = 0, rseq = 0;
                                size_t ackLen = tinylink::packet::unpack(
                                    _pBuf, dLen, &rtype, &rseq,
                                    ackBuf, sizeof(ackBuf));
                                if (ackLen == sizeof(TinyAck)) {
                                    _state = TinyState::WAIT_FOR_SYNC;
                                    if (_onAckReceived) {
                                        TinyAck ack;
                                        memcpy(&ack, ackBuf, sizeof(ack));
                                        _onAckReceived(ack);
                                    }
                                } else {
                                    _stats.increment(TinyStatus::ERR_CRC);
                                    _state = priorState;
                                }
                                continue;
                            }

                            // Log: always dispatched via dedicated callback.
                            if (wireType ==
                                    message_type_to_wire(MessageType::Log)) {
                                if (_onLogReceived) {
                                    LogMessage logMsg;
                                    uint8_t rtype = 0, rseq = 0;
                                    size_t logLen = tinylink::packet::unpack(
                                        _pBuf, dLen, &rtype, &rseq,
                                        reinterpret_cast<uint8_t*>(&logMsg),
                                        sizeof(logMsg));
                                    if (logLen == sizeof(LogMessage)) {
                                        _onLogReceived(logMsg);
                                    }
                                }
                                _state = (priorState == TinyState::AWAITING_ACK ||
                                          priorState == TinyState::CONNECTING)
                                         ? priorState
                                         : TinyState::WAIT_FOR_SYNC;
                                continue;
                            }
                        }

                        // ---- User data frame dispatch ----
                        {
                            uint8_t rtype = 0, rseq = 0;
                            size_t payloadLen = tinylink::packet::unpack(
                                _pBuf, dLen, &rtype, &rseq,
                                reinterpret_cast<uint8_t*>(&_data), sizeof(T));

                            if (payloadLen == sizeof(T)) {
                                _currType = rtype;
                                _currSeq  = rseq;
                                _stats.increment(TinyStatus::STATUS_OK);

                                // The post-dispatch idle state.
                                TinyState nextState =
                                    (priorState == TinyState::AWAITING_ACK)
                                    ? TinyState::AWAITING_ACK
                                    : TinyState::WAIT_FOR_SYNC;

                                if (_onDataReceived) {
                                    // Callback fires while state is FRAME_COMPLETE
                                    // so it can observe the dispatch moment.
                                    _onDataReceived(_data);
                                    _state = nextState;
                                } else {
                                    // Polling: transition first, then signal.
                                    _state  = nextState;
                                    _hasNew = true;
                                }
                                return; // yield until caller calls flush()
                            } else {
                                _stats.increment(TinyStatus::ERR_CRC);
                                _state = (priorState == TinyState::AWAITING_ACK ||
                                          priorState == TinyState::CONNECTING)
                                         ? priorState
                                         : TinyState::WAIT_FOR_SYNC;
                            }
                        }

                    } else {
                        // Frame too short — discard and resync.
                        _rawIdx = 0;
                        if (_state == TinyState::IN_FRAME ||
                                _state == TinyState::FRAME_COMPLETE) {
                            _state = TinyState::WAIT_FOR_SYNC;
                        }
                    }

                // ----------------------------------------------------------
                // Non-zero byte — accumulate into the raw buffer
                // ----------------------------------------------------------
                } else {
                    if (_rawIdx < sizeof(_rawBuf)) {
                        _rawBuf[_rawIdx++] = c;
                        if (_state == TinyState::WAIT_FOR_SYNC) {
                            _state = TinyState::IN_FRAME;
                        }
                    } else {
                        _stats.increment(TinyStatus::ERR_OVERFLOW);
                        _rawIdx = 0;
                        if (_state == TinyState::IN_FRAME) {
                            _state = TinyState::WAIT_FOR_SYNC;
                        }
                    }
                }
            } // while available
        } // update()


        // ---- sendData() — Encode and transmit a user data frame -------------
        //
        // Uses local stack buffers — never aliases the RX accumulation buffers.
        // In handshake mode the call is dropped silently until connected().
        // After a successful send, transitions to AWAITING_ACK (handshake mode).
        //
        void sendData(uint8_t type, const T& payload) {
            if (!_hw->isOpen()) return;
            if (_handshakeEnabled && !connected()) return;

            const size_t TX_PLAIN = 3 + sizeof(T) + 2;
            const size_t TX_ENC   = TX_PLAIN + 4;
            uint8_t pBuf[TX_PLAIN];
            uint8_t rawBuf[TX_ENC];

            _currSeq = _nextSeq++;
            size_t plainLen = tinylink::packet::pack(
                type, _currSeq,
                reinterpret_cast<const uint8_t*>(&payload), sizeof(T),
                pBuf, TX_PLAIN);
            if (plainLen == 0) { _stats.increment(TinyStatus::ERR_CRC); return; }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen,
                                                       rawBuf, TX_ENC);
            _hw->write(0x00);
            _hw->write(rawBuf, eLen);
            _hw->write(0x00);

            if (_handshakeEnabled) {
                _state    = TinyState::AWAITING_ACK;
                _lastSent = _hw->millis();
            }
        }


        // ---- sendLog() — Send a structured log frame -----------------------
        //
        // Wire format: LogMessage [ts:4][level:1][code:2][text:25] = 32 B.
        // The receiver can cast the raw payload directly to LogMessage.
        //
        bool sendLog(LogLevel level, uint16_t code, const char* text) {
            if (!_hw->isOpen()) return false;

            LogMessage msg;
            msg.ts    = static_cast<uint32_t>(_hw->millis());
            msg.level = static_cast<uint8_t>(level);
            msg.code  = code;
            logmessage_set_text(msg, text);

            _currSeq = _nextSeq++;
            return send_internal(
                message_type_to_wire(MessageType::Log), _currSeq,
                reinterpret_cast<const uint8_t*>(&msg), sizeof(msg),
                /*isInternal=*/false);
        }
    };

} // namespace tinylink

// ---------------------------------------------------------------------------
// Static member definition (template, so lives in the header)
// ---------------------------------------------------------------------------
template <typename T, typename Adapter, tinylink::adapter_check_t<Adapter> N>
tinylink::TinyLink<T, Adapter, N>* tinylink::TinyLink<T, Adapter, N>::_s_instance = nullptr;

#endif // TINY_LINK_H
