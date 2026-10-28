/**
 * @file TinyLink.h
 * @brief Main allocation-free engine for the TinyLink protocol.
 *
 * Consumer-defined payloads are opaque byte sequences. TinyLink preserves the
 * bytes supplied by the application but does not serialize C++ object fields.
 * Copying raw object memory is portable only when both peers intentionally use
 * the same representation, including padding, byte order, alignment, enum
 * representation, and ABI. Applications requiring cross-platform payloads
 * must encode and decode fields explicitly.
 *
 * The payload type must be trivially copyable, enforced at compile time on
 * every toolchain including AVR. That is only a minimum object-safety
 * requirement for copying the object representation with memcpy(); it does
 * not make the bytes portable between differing peers.
 */
#ifndef TINYLINK_H
#define TINYLINK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef __AVR__
#include <type_traits>
#include <utility>
#endif

#include "Version.h"
#include "adapters/WriteResult.h"
#include "internal/codec/CobsCodec.h"
#include "internal/codec/Packet.h"
#include "internal/protocol/AckMessage.h"
#include "internal/protocol/HandshakeMessage.h"
#include "internal/protocol/LogMessage.h"
#include "protocol/AckResult.h"
#include "protocol/LogLevel.h"
#include "protocol/MessageType.h"
#include "protocol/State.h"
#include "protocol/Stats.h"

namespace tinylink {

/**
 * @brief True when T may have its object representation copied with memcpy().
 *
 * avr-libc toolchains lack <type_traits>, so the equivalent compiler intrinsic
 * is used there; the requirement is identical on every platform.
 */
template <typename T>
struct is_payload_copyable {
#ifdef __AVR__
    static const bool value = __is_trivially_copyable(T);
#else
    static const bool value = std::is_trivially_copyable<T>::value;
#endif
};

#ifndef __AVR__
/** @brief Compile-time validation of the TinyLink transport adapter contract. */
template <typename Adapter>
struct is_valid_adapter {
private:
    template <typename Candidate>
    static auto check(int) -> decltype(
        static_cast<void>(static_cast<bool>(std::declval<Candidate>().isOpen())),
        static_cast<void>(static_cast<int>(std::declval<Candidate>().available())),
        static_cast<void>(static_cast<int>(std::declval<Candidate>().read())),
        static_cast<void>(std::declval<Candidate>().write(uint8_t(0))),
        static_cast<void>(std::declval<Candidate>().write(
            static_cast<const uint8_t*>(nullptr), size_t(0))),
        static_cast<void>(static_cast<unsigned long>(
            std::declval<Candidate>().millis())),
        std::integral_constant<bool,
        std::is_same<
            decltype(std::declval<Candidate&>().write(uint8_t(0))),
            WriteResult>::value &&
        std::is_same<
            decltype(std::declval<Candidate&>().write(
                static_cast<const uint8_t*>(nullptr), size_t(0))),
            WriteResult>::value>());

    template <typename>
    static std::false_type check(...);

public:
    static const bool value = decltype(check<Adapter>(0))::value;
};

template <typename Adapter>
using adapter_check_t =
    typename std::enable_if<is_valid_adapter<Adapter>::value, int>::type;
#else
template <typename Adapter>
using adapter_check_t = int;
#endif

/**
 * @brief TinyLink protocol engine.
 *
 * @tparam T Local application payload representation: an opaque byte
 * sequence to TinyLink. It must be trivially copyable (a minimum object-safety
 * requirement, not a portability guarantee) and sizeof(T) must not exceed the
 * protocol payload limit. TinyLink copies its object bytes without imposing a
 * portable field serialization; use explicit serialization when peers may
 * differ in padding, endianness, alignment, enum representation, or ABI.
 * @tparam Adapter Transport adapter satisfying the documented adapter API.
 */
template <typename T, typename Adapter, adapter_check_t<Adapter> = 0>
class TinyLink {
public:
    /** @brief Application payload callback. */
    typedef void (*DataCallback)(const T& data);

    /** @brief Decoded log-message callback. */
    typedef void (*LogCallback)(const LogMessage& message);

    /** @brief Validated handshake callback. */
    typedef void (*HandshakeCallback)(const HandshakeMessage& message);

    /** @brief Validated acknowledgement callback. */
    typedef void (*AckCallback)(uint8_t sequence, AckResult result);

private:
    static_assert(is_payload_copyable<T>::value,
                  "TinyLink payload type must be trivially copyable");
    static_assert(sizeof(T) <= packet::PACKET_MAX_PAYLOAD_SIZE,
                  "TinyLink application payload exceeds 64 bytes");

    static const size_t MAX_PLAIN_SIZE = packet::PACKET_MAX_SIZE;
    static const size_t MAX_ENCODED_SIZE =
        packet::PACKET_MAX_SIZE + packet::PACKET_MAX_SIZE / 254u + 1u;
    static const unsigned long HANDSHAKE_RETRY_MS = 1000ul;

    Adapter* _adapter;
    DataCallback _onDataReceived;
    LogCallback _onLogReceived;
    HandshakeCallback _onHandshakeReceived;
    AckCallback _onAckReceived;
    Stats _stats;
    ConnectionState _connectionState;
    ReceiveState _receiveState;
    TransmitState _transmitState;
    bool _handshakeEnabled;
    uint8_t _nextTransmitSequence;
    uint8_t _pendingTransmitSequence;
    uint8_t _lastReceivedSequence;
    bool _hasReceivedSequence;
    unsigned long _lastHandshakeSent;
    unsigned long _lastTransmit;
    unsigned long _lastReceiveByte;
    unsigned long _timeout;
    uint8_t _encodedReceive[MAX_ENCODED_SIZE];
    size_t _encodedReceiveLength;
    T _mailbox;
    bool _hasNew;
    bool _outputDirty;

    friend struct TinyLinkStatsAccess;

    TinyLink(const TinyLink&) = delete;
    TinyLink& operator=(const TinyLink&) = delete;

    /**
     * @brief Write a complete framed packet to the adapter.
     *
     * Succeeds only when both the encoded body and the delimiter were fully
     * accepted. A partial body or a failed delimiter leaves an unterminated
     * fragment on the wire; it cannot be resumed, so the call fails and the
     * next write first emits a delimiter to terminate the fragment. The peer
     * discards the fragment as an invalid frame.
     */
    bool write_frame(uint8_t type, uint8_t sequence,
                     const uint8_t* payload, size_t payloadLength)
    {
        if (!_adapter || !_adapter->isOpen()) {
            return false;
        }

        uint8_t plain[MAX_PLAIN_SIZE] = {};
        const packet::PacketResult packed = packet::pack(
            type, sequence, payload, payloadLength,
            plain, sizeof(plain));

        if (packed.status != packet::PacketStatus::Success) {
            _stats.increment(TelemetryEvent::PacketEncodeFailed);
            return false;
        }

        uint8_t encoded[MAX_ENCODED_SIZE] = {};
        const codec::CobsResult cobs = codec::cobs_encode(
            plain, packed.length, encoded, sizeof(encoded));

        if (cobs.status != codec::CobsStatus::Success) {
            _stats.increment(TelemetryEvent::CobsEncodeFailed);
            return false;
        }

        // A previously interrupted frame left unterminated bytes on the wire.
        // Partial writes cannot be resumed, so close the stale bytes with a
        // delimiter first; the peer discards them as one invalid frame.
        if (_outputDirty) {
            if (!_adapter->write(uint8_t(0)).complete()) {
                return false;
            }
            _outputDirty = false;
        }

        const WriteResult body = _adapter->write(encoded, cobs.bytesWritten);
        if (!body.complete()) {
            // Failed wrote nothing; Partial left an unterminated fragment.
            _outputDirty = body.status == WriteStatus::Partial;
            return false;
        }

        if (!_adapter->write(uint8_t(0)).complete()) {
            _outputDirty = true;
            return false;
        }

        return true;
    }

    /** @brief Send a handshake request or response. */
    bool send_handshake(HandshakeKind kind)
    {
        const HandshakeMessage message = {
            kind,
            TINYLINK_PROTOCOL_VERSION
        };
        uint8_t payload[HANDSHAKE_MESSAGE_WIRE_SIZE] = {};

        if (encode_handshake_message(
                message, payload, sizeof(payload)) !=
            HandshakeMessageCodecStatus::Success) {
            _stats.increment(TelemetryEvent::InvalidHandshake);
            return false;
        }

        const bool sent = write_frame(
            message_type_to_wire(MessageType::Handshake),
            0,
            payload,
            sizeof(payload));

        if (sent) {
            _lastHandshakeSent = _adapter->millis();
        }

        return sent;
    }

    /** @brief Route a validated plain packet by its reserved or application type. */
    void dispatch_packet(const uint8_t* plain, size_t plainLength)
    {
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t payloadLength = 0;

        const packet::PacketResult result = packet::unpack(
            plain, plainLength, &type, &sequence,
            payload, sizeof(payload), &payloadLength);

        if (result.status != packet::PacketStatus::Success) {
            if (result.status == packet::PacketStatus::ChecksumMismatch) {
                _stats.increment(TelemetryEvent::ChecksumMismatch);
            } else if (result.status == packet::PacketStatus::LengthMismatch ||
                       result.status == packet::PacketStatus::FrameTooShort) {
                _stats.increment(TelemetryEvent::PacketLengthMismatch);
            } else {
                _stats.increment(TelemetryEvent::PayloadSizeMismatch);
            }
            return;
        }

        if (type == message_type_to_wire(MessageType::Handshake)) {
            handle_handshake(payload, payloadLength);
            return;
        }

        if (type == message_type_to_wire(MessageType::Ack)) {
            handle_ack(sequence, payload, payloadLength);
            return;
        }

        if (type == message_type_to_wire(MessageType::Log)) {
            handle_log(payload, payloadLength);
            return;
        }

        handle_application(sequence, payload, payloadLength);
    }

    /** @brief Handle a handshake frame without exposing invalid data. */
    void handle_handshake(const uint8_t* payload, size_t payloadLength)
    {
        HandshakeMessage message = {HandshakeKind::Request, 0};
        const HandshakeMessageCodecStatus status =
            decode_handshake_message(payload, payloadLength, message);

        if (status != HandshakeMessageCodecStatus::Success) {
            _stats.increment(TelemetryEvent::InvalidHandshake);
            if (status == HandshakeMessageCodecStatus::UnsupportedVersion) {
                _connectionState = ConnectionState::Incompatible;
                _hasReceivedSequence = false;
            }
            return;
        }

        if (message.kind == HandshakeKind::Request) {
            // A peer restarting its handshake makes the old frame stale.
            _hasReceivedSequence = false;
        }

        if (_onHandshakeReceived) {
            _onHandshakeReceived(message);
        }

        if (message.kind == HandshakeKind::Request) {
            // Connect only once the response is out; otherwise the peer's
            // retried request gets another chance.
            if (send_handshake(HandshakeKind::Response)) {
                _connectionState = ConnectionState::Connected;
            }
        } else if (_connectionState == ConnectionState::Connecting) {
            _connectionState = ConnectionState::Connected;
        }
    }

    /** @brief Handle a sequence-correlated ACK frame. */
    void handle_ack(uint8_t sequence,
                    const uint8_t* payload,
                    size_t payloadLength)
    {
        AckResult result = AckResult::Unknown;
        const AckMessageCodecStatus status =
            decode_ack_message(payload, payloadLength, result);

        if (status != AckMessageCodecStatus::Success) {
            _stats.increment(TelemetryEvent::InvalidAckResult);
            return;
        }

        if (_transmitState != TransmitState::AwaitingAck ||
            sequence != _pendingTransmitSequence) {
            _stats.increment(TelemetryEvent::UnexpectedSequence);
            return;
        }

        _transmitState = TransmitState::Ready;

        if (_onAckReceived) {
            _onAckReceived(sequence, result);
        }
    }

    /** @brief Handle a log frame independently from callback registration. */
    void handle_log(const uint8_t* payload, size_t payloadLength)
    {
        LogMessage message = {};

        if (decode_log_message(payload, payloadLength, message) !=
            LogMessageCodecStatus::Success) {
            _stats.increment(TelemetryEvent::PayloadSizeMismatch);
            return;
        }

        if (_onLogReceived) {
            _onLogReceived(message);
        }
    }

    /** @brief Handle an application frame after reserved-type routing. */
    void handle_application(uint8_t sequence,
                            const uint8_t* payload,
                            size_t payloadLength)
    {
        if (!is_connected(_connectionState)) {
            _stats.increment(TelemetryEvent::UnexpectedMessageType);
            return;
        }

        if (payloadLength != sizeof(T)) {
            _stats.increment(TelemetryEvent::PayloadSizeMismatch);
            return;
        }

        memcpy(&_mailbox, payload, sizeof(T));
        _lastReceivedSequence = sequence;
        _hasReceivedSequence = true;
        _hasNew = true;
        _stats.increment(TelemetryEvent::PacketReceived);

        if (_onDataReceived) {
            _onDataReceived(_mailbox);
        }
    }

    /** @brief Decode and dispatch one accumulated COBS block. */
    void finish_frame()
    {
        if (_encodedReceiveLength == 0) {
            _receiveState = ReceiveState::WaitingForFrame;
            return;
        }

        uint8_t plain[MAX_PLAIN_SIZE] = {};
        const codec::CobsResult decoded = codec::cobs_decode(
            _encodedReceive,
            _encodedReceiveLength,
            plain,
            sizeof(plain));

        _encodedReceiveLength = 0;
        _receiveState = ReceiveState::WaitingForFrame;

        if (decoded.status != codec::CobsStatus::Success) {
            _stats.increment(TelemetryEvent::CobsDecodeFailed);
            return;
        }

        dispatch_packet(plain, decoded.bytesWritten);
    }

    /** @brief Consume one transport byte using discard-until-delimiter overflow recovery. */
    void consume_byte(uint8_t value)
    {
        _lastReceiveByte = _adapter->millis();

        if (value == 0) {
            if (_receiveState == ReceiveState::DiscardingFrame) {
                _encodedReceiveLength = 0;
                _receiveState = ReceiveState::WaitingForFrame;
                return;
            }

            finish_frame();
            return;
        }

        if (_receiveState == ReceiveState::DiscardingFrame) {
            return;
        }

        if (_encodedReceiveLength >= sizeof(_encodedReceive)) {
            _stats.increment(TelemetryEvent::ReceiveOverflow);
            _encodedReceiveLength = 0;
            _receiveState = ReceiveState::DiscardingFrame;
            return;
        }

        _encodedReceive[_encodedReceiveLength++] = value;
        _receiveState = ReceiveState::ReceivingFrame;
    }

public:
    /** @brief Construct a TinyLink engine over an existing adapter. */
    explicit TinyLink(Adapter& adapter)
        : _adapter(&adapter),
          _onDataReceived(nullptr),
          _onLogReceived(nullptr),
          _onHandshakeReceived(nullptr),
          _onAckReceived(nullptr),
          _stats(),
          _connectionState(ConnectionState::Inactive),
          _receiveState(ReceiveState::WaitingForFrame),
          _transmitState(TransmitState::Ready),
          _handshakeEnabled(false),
          _nextTransmitSequence(0),
          _pendingTransmitSequence(0),
          _lastReceivedSequence(0),
          _hasReceivedSequence(false),
          _lastHandshakeSent(0),
          _lastTransmit(0),
          _lastReceiveByte(0),
          _timeout(1000ul),
          _encodedReceive(),
          _encodedReceiveLength(0),
          _mailbox(),
          _hasNew(false),
          _outputDirty(false)
    {}

    /** @brief Register an application payload callback. */
    void onDataReceived(DataCallback callback) { _onDataReceived = callback; }

    /** @brief Register a decoded log callback. */
    void onLogReceived(LogCallback callback) { _onLogReceived = callback; }

    /** @brief Register a validated handshake callback. */
    void onHandshakeReceived(HandshakeCallback callback)
    {
        _onHandshakeReceived = callback;
    }

    /** @brief Register a sequence-correlated acknowledgement callback. */
    void onAckReceived(AckCallback callback) { _onAckReceived = callback; }

    /** @brief Set receive and acknowledgement timeout in milliseconds. */
    void setTimeout(unsigned long milliseconds) { _timeout = milliseconds; }

    /**
     * @brief Start protocol-version handshake negotiation.
     *
     * @return true if the first handshake request was completely written.
     * On false the engine is still Connecting and update() retries the
     * request at the handshake retry interval.
     */
    bool begin()
    {
        _handshakeEnabled = true;
        _connectionState = ConnectionState::Connecting;
        _hasReceivedSequence = false;
        return send_handshake(HandshakeKind::Request);
    }

    /** @brief Reset all runtime protocol state while preserving callbacks and timeout. */
    void reset()
    {
        _connectionState = ConnectionState::Inactive;
        _receiveState = ReceiveState::WaitingForFrame;
        _transmitState = TransmitState::Ready;
        _handshakeEnabled = false;
        _encodedReceiveLength = 0;
        _hasNew = false;
        _nextTransmitSequence = 0;
        _pendingTransmitSequence = 0;
        _lastReceivedSequence = 0;
        _hasReceivedSequence = false;
    }

    /** @brief Process timers and all currently available transport bytes. */
    void update()
    {
        if (!_adapter) {
            return;
        }

        const unsigned long now = _adapter->millis();

        if (_handshakeEnabled &&
            _connectionState == ConnectionState::Connecting &&
            now - _lastHandshakeSent >= HANDSHAKE_RETRY_MS) {
            send_handshake(HandshakeKind::Request);
        }

        if (_receiveState == ReceiveState::ReceivingFrame &&
            now - _lastReceiveByte >= _timeout) {
            _stats.increment(TelemetryEvent::InterByteTimeout);
            _encodedReceiveLength = 0;
            _receiveState = ReceiveState::WaitingForFrame;
        }

        if (_transmitState == TransmitState::AwaitingAck &&
            now - _lastTransmit >= _timeout) {
            _stats.increment(TelemetryEvent::AckTimeout);
            _transmitState = TransmitState::Ready;
        }

        while (_adapter->available() > 0) {
            const int input = _adapter->read();
            if (input < 0) {
                break;
            }
            consume_byte(static_cast<uint8_t>(input));
        }
    }

    /** @brief Return whether a compatible connection is established. */
    bool connected() const { return is_connected(_connectionState); }

    /** @brief Return stable connection lifecycle state. */
    ConnectionState connectionState() const { return _connectionState; }

    /** @brief Return whether one acknowledged transmission is pending. */
    bool awaitingAck() const { return is_awaiting_ack(_transmitState); }

    /** @brief Return whether the engine can send another application frame. */
    bool canSend() const { return can_send(_connectionState, _transmitState); }

    /** @brief Return a copy of current local telemetry counters. */
    Stats stats() const { return _stats; }

    /** @brief Reset all local telemetry counters. */
    void clearStats() { _stats.clear(); }

    /** @brief Return whether a new application payload is available. */
    bool available() const { return _hasNew; }

    /** @brief Return the latest application payload and clear availability. */
    T read()
    {
        _hasNew = false;
        return _mailbox;
    }

    /** @brief Clear the application payload availability flag. */
    void flush() { _hasNew = false; }

    /**
     * @brief Send one application payload and await its correlated ACK.
     *
     * Reserved internal message-type bytes are rejected. The object bytes are
     * sent unchanged and therefore require an application-defined portability
     * contract when peers use different platforms or ABIs.
     */
    bool sendData(uint8_t type, const T& payload)
    {
        if (!canSend() || is_reserved_message_type(type)) {
            return false;
        }

        // The sequence is consumed only after the whole frame was written.
        const uint8_t sequence = _nextTransmitSequence;
        if (!write_frame(
                type,
                sequence,
                reinterpret_cast<const uint8_t*>(&payload),
                sizeof(T))) {
            return false;
        }

        ++_nextTransmitSequence;
        _pendingTransmitSequence = sequence;
        _lastTransmit = _adapter->millis();
        _transmitState = TransmitState::AwaitingAck;
        return true;
    }

    /**
     * @brief Acknowledge the most recently accepted application frame.
     *
     * Returns false without writing until an application frame has been
     * accepted. The acknowledged sequence stays valid across callback
     * delivery, read() and flush(). It is invalidated by construction,
     * reset(), begin(), a peer handshake request (peer restart) and an
     * unsupported handshake version (Incompatible).
     */
    bool sendAck(AckResult result = AckResult::Ok)
    {
        if (!_hasReceivedSequence) {
            return false;
        }

        uint8_t payload[ACK_MESSAGE_WIRE_SIZE] = {};
        if (encode_ack_message(result, payload, sizeof(payload)) !=
            AckMessageCodecStatus::Success) {
            return false;
        }

        return write_frame(
            message_type_to_wire(MessageType::Ack),
            _lastReceivedSequence,
            payload,
            sizeof(payload));
    }

    /** @brief Send an explicitly encoded structured log frame. */
    bool sendLog(LogLevel level, uint16_t code, const char* text)
    {
        LogMessage message = {};
        message.uptimeMilliseconds =
            static_cast<uint32_t>(_adapter->millis());
        message.level = level;
        message.code = code;
        set_log_message_text(message, text);

        uint8_t payload[LOG_MESSAGE_WIRE_SIZE] = {};
        if (encode_log_message(message, payload, sizeof(payload)) !=
            LogMessageCodecStatus::Success) {
            return false;
        }

        if (!write_frame(
                message_type_to_wire(MessageType::Log),
                _nextTransmitSequence,
                payload,
                sizeof(payload))) {
            return false;
        }

        ++_nextTransmitSequence;
        return true;
    }
};

} // namespace tinylink

#endif // TINYLINK_H
