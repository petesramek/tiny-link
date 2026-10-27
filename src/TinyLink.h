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
#include "TinyWriteResult.h"
#include <stdint.h>
#include <string.h>

#ifndef __AVR__
#include <type_traits>
#endif

namespace tinylink {

    namespace detail {
#ifdef __AVR__
        // The AVR toolchain's libstdc++ does not provide <type_traits>.
        template <typename T>
        struct is_trivially_copyable {
            static const bool value = __is_trivially_copyable(T);
        };
#else
        template <typename T>
        struct is_trivially_copyable : std::is_trivially_copyable<T> {};
#endif
    }

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

    // User payloads are opaque byte sequences. The trivially-copyable check
    // makes copying their object representation safe; it does not define a
    // portable wire format.
    template <typename T, typename Adapter, adapter_check_t<Adapter> = 0>
    class TinyLink {
        static_assert(detail::is_trivially_copyable<T>::value,
            "TinyLink: Payload must be trivially copyable.");

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
        uint8_t       _receivedSeq; ///< Sequence of the last accepted application frame
        uint8_t       _nextSeq;
        bool          _hasNew;
        bool          _hasReceivedSequence; ///< Whether _receivedSeq may be acknowledged
        unsigned long _lastByte;
        unsigned long _timeout;

        TinyState     _state;
        bool          _handshakeEnabled;
        bool          _transportLost;
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
            uint8_t rawBuf[MAX_ENC + 2];

            size_t plainLen = tinylink::packet::pack(wireType, seq, payload, len,
                                                     pBuf, MAX_PLAIN);
            if (plainLen == 0) {
                if (!isInternal) _stats.increment(TelemetryEvent::CrcError);
                return false;
            }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen,
                                                       rawBuf + 1, MAX_ENC);
            return writeEncodedFrame(rawBuf, eLen);
        }

        // A frame is submitted as one adapter write so a partial result is
        // unambiguously incomplete: the final delimiter is the last byte.
        bool writeEncodedFrame(uint8_t* frame, size_t encodedLen) {
            if (encodedLen > MAX_FRAME_ENC) return false;
            frame[0] = 0x00;
            frame[encodedLen + 1] = 0x00;
            const size_t frameLen = encodedLen + 2;
            TinyWriteResult result = _hw->write(frame, frameLen);
            return result.status == TinyWriteStatus::Complete &&
                   result.bytesWritten == frameLen;
        }

        // ---- State utility --------------------------------------------------
        //
        // Returns priorState unchanged for connection-phase states
        // (AWAITING_ACK, CONNECTING), otherwise returns WAIT_FOR_SYNC.
        // Used whenever a non-data event must not disturb an in-progress
        // connection or ACK wait.
        //
        TinyState restoreState(TinyState priorState) const {
            return (priorState == TinyState::AWAITING_ACK ||
                    priorState == TinyState::CONNECTING)
                   ? priorState
                   : TinyState::WAIT_FOR_SYNC;
        }

        void markTransportLost() {
            if (_transportLost) return;
            _transportLost = true;
            _state = TinyState::FAILED;
            _rawIdx = 0;
            _lastByte = 0;
            _lastSent = 0;
            _lastHandshakeSent = 0;
            _hasReceivedSequence = false;
        }

        void restoreTransport(unsigned long now) {
            if (!_transportLost) return;
            _transportLost = false;
            _rawIdx = 0;
            _lastByte = 0;
            _lastSent = 0;
            _hasReceivedSequence = false;

            if (!_handshakeEnabled) {
                _state = TinyState::WAIT_FOR_SYNC;
                return;
            }

            _state = TinyState::CONNECTING;
            _lastHandshakeSent = now;
            HandshakeMessage m;
            m.version = 0;
            send_internal(message_type_to_wire(MessageType::Handshake), 0,
                          reinterpret_cast<const uint8_t*>(&m), sizeof(m),
                          /*isInternal=*/true);
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
        //     → advance out of CONNECTING regardless of which peer initiated
        //
        //   Receive HS(v=1) from peer
        //     → peer acknowledged our begin(); advance out of CONNECTING
        //     → if already connected, ignore to prevent ping-pong
        //
        // In both cases the resulting state is the same:
        //   AWAITING_ACK  if we were already waiting for a data ACK
        //   WAIT_FOR_SYNC otherwise (including the CONNECTING → connected path)
        //
        // handleHandshakeMessage receives priorState (the state before we
        // entered FRAME_COMPLETE) so it can make correct transitions
        // regardless of the fact that _state is FRAME_COMPLETE at call time.
        bool handleHandshakeMessage(const uint8_t* payload, size_t len,
                                    TinyState fromState) {
            if (len < sizeof(HandshakeMessage)) return false;

            HandshakeMessage m;
            memcpy(&m, payload, sizeof(m));

            // Initial connection request: reply with HS(v=1).
            if (m.version == 0) {
                HandshakeMessage reply;
                reply.version = 1;
                if (!send_internal(message_type_to_wire(MessageType::Handshake), 0,
                                   reinterpret_cast<const uint8_t*>(&reply),
                                   sizeof(reply), /*isInternal=*/true)) {
                    _state = restoreState(fromState);
                    return false;
                }
            }

            // For both HS(v=0) and HS(v=1): we are now connected.
            // Preserve AWAITING_ACK if a data ACK is still pending;
            // otherwise move to the connected idle state.
            _hasReceivedSequence = false;
            _state = (fromState == TinyState::AWAITING_ACK)
                     ? TinyState::AWAITING_ACK
                     : TinyState::WAIT_FOR_SYNC;
            return true;
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
              _rawIdx(0), _currType(0), _currSeq(0), _receivedSeq(0), _nextSeq(0),
              _hasNew(false), _hasReceivedSequence(false), _lastByte(0), _timeout(250),
              _state(TinyState::WAIT_FOR_SYNC),
              _handshakeEnabled(false), _transportLost(false),
              _lastHandshakeSent(0), _lastSent(0)
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
         * CONNECTING. While CONNECTING the engine periodically re-sends the
         * Handshake (every HANDSHAKE_RETRY_MS = 1 s) until the peer replies.
         *
         * @return true if the complete request frame was accepted by the
         *         adapter; false if the port is closed or output was incomplete.
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
         * After an observed transport loss, update() automatically starts a
         * fresh handshake when the adapter becomes available again.
         */
        bool begin() {
            _handshakeEnabled = true;
            if (!_hw->isOpen()) {
                markTransportLost();
                return false;
            }
            _transportLost = false;
            _rawIdx = 0;
            _hasReceivedSequence = false;
            _state            = TinyState::CONNECTING;
            _lastHandshakeSent = _hw->millis();
            HandshakeMessage m;
            m.version = 0;
            return send_internal(message_type_to_wire(MessageType::Handshake), 0,
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
            _transportLost     = false;
            _rawIdx            = 0;
            _hasNew            = false;
            _currType          = 0;
            _currSeq           = 0;
            _receivedSeq       = 0;
            _nextSeq           = 0;
            _hasReceivedSequence = false;
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
         * @brief True when the transport is available and the protocol is ready.
         *
         * This reports both transport availability and protocol readiness.
         * Basic mode (begin() never called): true while the adapter is open
         * and the engine is not in FAILED.
         *
         * Handshake mode (after begin()): true only once CONNECTING has
         * resolved to a data-phase state, and while the adapter is open.
         * Call update() regularly to observe transport loss and restoration.
         */
        bool connected() const {
            if (!_hw->isOpen() || _transportLost ||
                    _state == TinyState::FAILED) return false;
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

        // ---- update() helpers -----------------------------------------------

        // Periodic Handshake re-send while awaiting the peer.
        void tickHandshake(unsigned long now) {
            if (!_handshakeEnabled || _state != TinyState::CONNECTING) return;
            if (now - _lastHandshakeSent < HANDSHAKE_RETRY_MS) return;
            _lastHandshakeSent = now;
            HandshakeMessage m;
            m.version = 0;
            send_internal(message_type_to_wire(MessageType::Handshake), 0,
                          reinterpret_cast<const uint8_t*>(&m), sizeof(m),
                          /*isInternal=*/true);
        }

        // Inter-byte timeout (stalled partial frame) and ACK timeout.
        void tickTimeouts(unsigned long now) {
            if (_rawIdx > 0 && (now - _lastByte > _timeout)) {
                _rawIdx = 0;
                _stats.increment(TelemetryEvent::Timeout);
                if (_state == TinyState::IN_FRAME) {
                    _state = TinyState::WAIT_FOR_SYNC;
                }
                // CONNECTING / AWAITING_ACK: partial frame discarded, but the
                // connection-phase state is preserved.
            }
            if (_handshakeEnabled &&
                    _state == TinyState::AWAITING_ACK && _rawIdx == 0 &&
                    now - _lastSent > _timeout) {
                _stats.increment(TelemetryEvent::Timeout);
                _state = TinyState::WAIT_FOR_SYNC;
            }
        }

        // Non-zero byte: append to the raw buffer or flag overflow.
        void accumulateByte(uint8_t c) {
            if (_rawIdx < sizeof(_rawBuf)) {
                _rawBuf[_rawIdx++] = c;
                if (_state == TinyState::WAIT_FOR_SYNC) {
                    _state = TinyState::IN_FRAME;
                }
            } else {
                _stats.increment(TelemetryEvent::Overflow);
                _rawIdx = 0;
                if (_state == TinyState::IN_FRAME) {
                    _state = TinyState::WAIT_FOR_SYNC;
                }
            }
        }

        // Handshake: unpack, update state, fire optional callback.
        void dispatchHandshakeFrame(size_t dLen, TinyState priorState) {
            uint8_t hsBuf[sizeof(HandshakeMessage)];
            uint8_t rtype = 0, rseq = 0;
            size_t hsLen = tinylink::packet::unpack(
                _pBuf, dLen, &rtype, &rseq, hsBuf, sizeof(hsBuf));
            if (hsLen == sizeof(HandshakeMessage)) {
                (void)handleHandshakeMessage(hsBuf, hsLen, priorState);
                if (_onHandshakeReceived) {
                    HandshakeMessage hm;
                    memcpy(&hm, hsBuf, sizeof(hm));
                    _onHandshakeReceived(hm);
                }
            } else {
                _state = restoreState(priorState);
            }
        }

        // Ack: unpack, clear AWAITING_ACK, fire optional callback.
        void dispatchAckFrame(size_t dLen, TinyState priorState) {
            uint8_t ackBuf[sizeof(TinyAck)];
            uint8_t rtype = 0, rseq = 0;
            size_t ackLen = tinylink::packet::unpack(
                _pBuf, dLen, &rtype, &rseq, ackBuf, sizeof(ackBuf));
            if (ackLen == sizeof(TinyAck)) {
                _state = TinyState::WAIT_FOR_SYNC;
                if (_onAckReceived) {
                    TinyAck ack;
                    memcpy(&ack, ackBuf, sizeof(ack));
                    _onAckReceived(ack);
                }
            } else {
                _stats.increment(TelemetryEvent::CrcError);
                _state = priorState;
            }
        }

        // Log: unpack and fire optional callback; preserve connection state.
        void dispatchLogFrame(size_t dLen, TinyState priorState) {
            if (_onLogReceived) {
                LogMessage logMsg;
                uint8_t rtype = 0, rseq = 0;
                size_t logLen = tinylink::packet::unpack(
                    _pBuf, dLen, &rtype, &rseq,
                    reinterpret_cast<uint8_t*>(&logMsg), sizeof(logMsg));
                if (logLen == sizeof(LogMessage)) {
                    _onLogReceived(logMsg);
                }
            }
            _state = restoreState(priorState);
        }

        // User data: unpack, update state, fire callback or signal polling.
        // Returns true when the caller (update()) should yield until flush().
        bool dispatchDataFrame(size_t dLen, TinyState priorState) {
            uint8_t rtype = 0, rseq = 0;
            size_t payloadLen = tinylink::packet::unpack(
                _pBuf, dLen, &rtype, &rseq,
                reinterpret_cast<uint8_t*>(&_data), sizeof(T));

            if (payloadLen != sizeof(T)) {
                _stats.increment(TelemetryEvent::CrcError);
                _state = restoreState(priorState);
                return false;
            }

            _currType = rtype;
            _currSeq  = rseq;
            _receivedSeq = rseq;
            _hasReceivedSequence = true;
            _stats.increment(TelemetryEvent::PacketReceived);

            // Post-dispatch idle state: preserve AWAITING_ACK if a data ACK
            // is still pending for a frame we sent earlier.
            TinyState nextState = (priorState == TinyState::AWAITING_ACK)
                                  ? TinyState::AWAITING_ACK
                                  : TinyState::WAIT_FOR_SYNC;

            if (_onDataReceived) {
                // Callback fires while state is FRAME_COMPLETE so it can
                // observe the dispatch moment.
                _onDataReceived(_data);
                // If the callback replied via sendData() the state has already
                // advanced (e.g. to AWAITING_ACK). Only apply the idle
                // nextState when the callback left _state unchanged.
                if (_state == TinyState::FRAME_COMPLETE) {
                    _state = nextState;
                }
                return false;
            }

            // Polling: transition first, then signal.
            _state  = nextState;
            _hasNew = true;
            return true; // yield until caller calls flush()
        }

        // 0x00 delimiter received: decode the accumulated frame and dispatch
        // it to the appropriate handler. Returns true when update() should yield.
        bool onFrameBoundary() {
            if (_rawIdx < 5) {
                // Frame too short — discard and resync.
                _rawIdx = 0;
                if (_state == TinyState::IN_FRAME ||
                        _state == TinyState::FRAME_COMPLETE) {
                    _state = TinyState::WAIT_FOR_SYNC;
                }
                return false;
            }

            TinyState priorState = _state;
            _state = TinyState::FRAME_COMPLETE;
            size_t dLen = tinylink::codec::cobs_decode(
                _rawBuf, _rawIdx, _pBuf, sizeof(_pBuf));
            _rawIdx = 0;

            if (dLen >= 6) {
                uint8_t wireType = _pBuf[0];

                if (wireType == message_type_to_wire(MessageType::Handshake)) {
                    dispatchHandshakeFrame(dLen, priorState);
                    return false;
                }
                if (wireType == message_type_to_wire(MessageType::Ack) &&
                        priorState == TinyState::AWAITING_ACK) {
                    dispatchAckFrame(dLen, priorState);
                    return false;
                }
                if (wireType == message_type_to_wire(MessageType::Log)) {
                    dispatchLogFrame(dLen, priorState);
                    return false;
                }
            }

            return dispatchDataFrame(dLen, priorState);
        }

        // ---- update() — Main RX / State-Machine Driver ----------------------
        //
        // Call as often as possible from the main loop (or a timer ISR-safe
        // context).  Reads all bytes available from the adapter, drives the
        // RX state machine, and dispatches complete frames.
        //
        void update() {
            if (!_hw->isOpen()) {
                markTransportLost();
                return;
            }

            unsigned long now = _hw->millis();
            restoreTransport(now);
            if (_hasNew) return;
            tickHandshake(now);
            tickTimeouts(now);

            while (_hw->available() > 0) {
                int incoming = _hw->read();
                if (incoming < 0) break;

                _lastByte = _hw->millis();
                uint8_t c = static_cast<uint8_t>(incoming);

                if (c == 0x00) {
                    if (onFrameBoundary()) return; // yield until flush()
                } else {
                    accumulateByte(c);
                }
            }
        } // update()


        // ---- sendData() — Encode and transmit a user data frame -------------
        //
        // Uses local stack buffers — never aliases the RX accumulation buffers.
        // In handshake mode the call is rejected until connected().
        // Returns false if output is incomplete; only a complete frame consumes
        // a sequence number or transitions to AWAITING_ACK.
        //
        // @return true if the complete frame was accepted; false if the port
        //         is closed, the link is not connected, or output was incomplete.
        bool sendData(uint8_t type, const T& payload) {
            if (!_hw->isOpen()) {
                markTransportLost();
                return false;
            }
            if (_transportLost || _state == TinyState::FAILED) return false;
            if (_handshakeEnabled && !connected()) return false;

            const size_t TX_PLAIN = 3 + sizeof(T) + 2;
            const size_t TX_ENC   = TX_PLAIN + 4;
            uint8_t pBuf[TX_PLAIN];
            uint8_t rawBuf[TX_ENC + 2];

            const uint8_t seq = _nextSeq;
            size_t plainLen = tinylink::packet::pack(
                type, seq,
                reinterpret_cast<const uint8_t*>(&payload), sizeof(T),
                pBuf, TX_PLAIN);
            if (plainLen == 0) {
                _stats.increment(TelemetryEvent::CrcError);
                return false;
            }
            size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen,
                                                       rawBuf + 1, TX_ENC);
            if (!writeEncodedFrame(rawBuf, eLen)) {
                _state = restoreState(_state);
                return false;
            }

            _currSeq = seq;
            _nextSeq = static_cast<uint8_t>(seq + 1);
            if (_handshakeEnabled) {
                _state    = TinyState::AWAITING_ACK;
                _lastSent = _hw->millis();
            }
            return true;
        }

        // ---- sendAck() — Send an ACK for the last received user data frame --
        //
        // Call this from onDataReceived() (or from polling code after peek())
        // when the peer has handshake mode enabled (begin() was called) and is
        // waiting in AWAITING_ACK.  The peer exits that state immediately rather
        // than waiting for the ACK timeout.
        //
        // Calling sendAck() from inside onDataReceived() is safe and recommended:
        //   void onData(const T& d) {
        //       link.sendAck();           // release peer from AWAITING_ACK
        //       link.sendData(TYPE, reply); // send response (→ AWAITING_ACK)
        //   }
        //
        // If begin() was never called (no handshake mode), the peer is never in
        // AWAITING_ACK, so this call has no observable effect.
        //
        // @param result  Status code to include in the ACK (default STATUS_OK).
        // @return true if the complete ACK frame was accepted; false if no
        //         valid application frame has been received, the port is closed,
        //         or output was incomplete.
        //
        bool sendAck(TinyStatus result = TinyStatus::STATUS_OK) {
            if (!_hw->isOpen()) {
                markTransportLost();
                return false;
            }
            if (_transportLost || _state == TinyState::FAILED) return false;
            if (!_hasReceivedSequence) return false;
            TinyAck ack;
            ack.seq    = _receivedSeq;
            ack.result = result;
            return send_internal(
                message_type_to_wire(MessageType::Ack), _receivedSeq,
                reinterpret_cast<const uint8_t*>(&ack), sizeof(ack),
                /*isInternal=*/true);
        }


        // ---- sendLog() — Send a structured log frame -----------------------
        //
        // Wire format: LogMessage [ts:4][level:1][code:2][text:25] = 32 B.
        // The receiver can cast the raw payload directly to LogMessage.
        // A failed write does not consume a transmit sequence number.
        // @return true if the complete frame was accepted; false if the port
        //         is closed or output was incomplete.
        //
        bool sendLog(LogLevel level, uint16_t code, const char* text) {
            if (!_hw->isOpen()) {
                markTransportLost();
                return false;
            }
            if (_transportLost || _state == TinyState::FAILED) return false;

            LogMessage msg;
            msg.ts    = static_cast<uint32_t>(_hw->millis());
            msg.level = static_cast<uint8_t>(level);
            msg.code  = code;
            logmessage_set_text(msg, text);

            const uint8_t seq = _nextSeq;
            if (!send_internal(
                    message_type_to_wire(MessageType::Log), seq,
                    reinterpret_cast<const uint8_t*>(&msg), sizeof(msg),
                    /*isInternal=*/false)) {
                return false;
            }

            _currSeq = seq;
            _nextSeq = static_cast<uint8_t>(seq + 1);
            return true;
        }
    };

} // namespace tinylink

// ---------------------------------------------------------------------------
// Static member definition (template, so lives in the header)
// ---------------------------------------------------------------------------
template <typename T, typename Adapter, tinylink::adapter_check_t<Adapter> N>
tinylink::TinyLink<T, Adapter, N>* tinylink::TinyLink<T, Adapter, N>::_s_instance = nullptr;

#endif // TINY_LINK_H
