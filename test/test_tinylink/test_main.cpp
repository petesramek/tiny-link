#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "TinyLinkTestHelpers.h"

namespace {

    struct Payload {
        uint8_t bytes[4];
    };

    struct MaximumPayload {
        uint8_t bytes[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE];
    };

    typedef tinylink_test::FakeAdapter FakeAdapter;
    typedef tinylink::TinyLink<Payload, FakeAdapter> Link;

    unsigned int dataCallbackCount;
    unsigned int logCallbackCount;
    unsigned int handshakeCallbackCount;
    unsigned int ackCallbackCount;
    Payload lastData;
    tinylink::LogMessage lastLog;
    tinylink::HandshakeMessage lastHandshake;
    uint8_t lastAckSequence;
    tinylink::AckResult lastAckResult;
    Link* ackOnCallbackLink;

    void onData(const Payload& payload)
    {
        ++dataCallbackCount;
        lastData = payload;
    }

    void onLog(const tinylink::LogMessage& message)
    {
        ++logCallbackCount;
        lastLog = message;
    }

    void onHandshake(const tinylink::HandshakeMessage& message)
    {
        ++handshakeCallbackCount;
        lastHandshake = message;
    }

    void onAck(uint8_t sequence, tinylink::AckResult result)
    {
        ++ackCallbackCount;
        lastAckSequence = sequence;
        lastAckResult = result;
    }

    void acknowledgeOnData(const Payload&)
    {
        ackOnCallbackLink->sendAck();
    }

    Payload payloadWith(uint8_t first)
    {
        Payload payload = {{ first, static_cast<uint8_t>(first + 1),
            static_cast<uint8_t>(first + 2), static_cast<uint8_t>(first + 3) }};
        return payload;
    }

    bool connectPeer(FakeAdapter& adapter, Link& link)
    {
        if (!tinylink_test::queueHandshake(
                adapter, tinylink::HandshakeKind::Request)) {
            return false;
        }
        link.update();
        adapter.clearOutput();
        return link.connected();
    }

    void testConstructionStartsInactiveAndReadyWithZeroTelemetry()
    {
        FakeAdapter adapter;
        Link link(adapter);

        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.sendAck());

        const tinylink::Stats stats = link.stats();
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(0, stats.cobsDecodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.checksumMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetLengthMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.payloadSizeMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetEncodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.cobsEncodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.interByteTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, stats.ackTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, stats.receiveOverflows);
        TEST_ASSERT_EQUAL_UINT32(0, stats.unexpectedMessageTypes);
        TEST_ASSERT_EQUAL_UINT32(0, stats.unexpectedSequences);
        TEST_ASSERT_EQUAL_UINT32(0, stats.invalidAckResults);
        TEST_ASSERT_EQUAL_UINT32(0, stats.invalidHandshakes);

        const Payload initial = link.read();
        const uint8_t zeroes[sizeof(Payload)] = {};
        TEST_ASSERT_EQUAL_UINT8_ARRAY(zeroes, initial.bytes, sizeof(initial.bytes));
        TEST_ASSERT_FALSE(link.available());
    }

    void testBeginSendsVersionedHandshakeRequestAndEnablesConnectingState()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.begin();

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake), type);
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
        tinylink::HandshakeMessage message = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_handshake_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Request),
            static_cast<int>(message.kind));
        TEST_ASSERT_EQUAL_UINT8(TINYLINK_PROTOCOL_VERSION, message.protocolVersion);
    }

    void testHandshakeRequestConnectsAndSendsCompatibleResponse()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onHandshakeReceived(onHandshake);

        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();

        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connected),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_TRUE(link.canSend());
        TEST_ASSERT_EQUAL_UINT32(1, handshakeCallbackCount);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Request),
            static_cast<int>(lastHandshake.kind));
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[2] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake), type);
        tinylink::HandshakeMessage response = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_handshake_message(
                payload, payloadLength, response)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Response),
            static_cast<int>(response.kind));
        TEST_ASSERT_EQUAL_UINT8(TINYLINK_PROTOCOL_VERSION, response.protocolVersion);
    }

    void testHandshakeResponseConnectsOnlyAnInitiatingLink()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.begin();
        link.onHandshakeReceived(onHandshake);
        adapter.clearOutput();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_EQUAL_UINT32(1, handshakeCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));

        FakeAdapter unsolicitedAdapter;
        Link unsolicited(unsolicitedAdapter);
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            unsolicitedAdapter, tinylink::HandshakeKind::Response));
        unsolicited.update();
        TEST_ASSERT_FALSE(unsolicited.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(unsolicited.connectionState()));
    }

    void testHandshakeRetriesAtTheConfiguredProtocolInterval()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.begin();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        adapter.advanceTime(999);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(2, tinylink_test::writtenFrameCount(adapter));

        adapter.advanceTime(999);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(2, tinylink_test::writtenFrameCount(adapter));

        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(3, tinylink_test::writtenFrameCount(adapter));
    }

    void testUnsupportedHandshakeVersionMarksLinkIncompatible()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.begin();
        adapter.clearOutput();

        const uint8_t unsupportedVersion[] = {
            static_cast<uint8_t>(tinylink::HandshakeKind::Response),
            static_cast<uint8_t>(TINYLINK_PROTOCOL_VERSION + 1)
        };
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0,
            unsupportedVersion,
            sizeof(unsupportedVersion)));
        link.update();

        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Incompatible),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().invalidHandshakes);
        TEST_ASSERT_EQUAL_UINT32(0, handshakeCallbackCount);
    }

    void testMalformedHandshakeDoesNotConnectOrInvokeCallback()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.begin();
        adapter.clearOutput();
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0,
            nullptr,
            0));
        const uint8_t invalidKind[] = { 0x7Fu, TINYLINK_PROTOCOL_VERSION };
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0,
            invalidKind,
            sizeof(invalidKind)));
        link.update();

        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_EQUAL_UINT32(2, link.stats().invalidHandshakes);
        TEST_ASSERT_EQUAL_UINT32(0, handshakeCallbackCount);
    }

    void testApplicationSendUsesOpaquePayloadAndCorrelatedAcknowledgement()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        link.onAckReceived(onAck);

        const Payload payload = payloadWith(0x31);
        TEST_ASSERT_TRUE(link.sendData(0x20, payload));
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_FALSE(link.sendData(0x21, payload));
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t decoded[sizeof(Payload)] = {};
        size_t decodedLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, decoded, sizeof(decoded), decodedLength));
        TEST_ASSERT_EQUAL_UINT8(0x20, type);
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
        TEST_ASSERT_EQUAL_UINT32(sizeof(payload), decodedLength);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.bytes, decoded, sizeof(payload.bytes));

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, sequence, tinylink::AckResult::Busy));
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_TRUE(link.canSend());
        TEST_ASSERT_EQUAL_UINT32(1, ackCallbackCount);
        TEST_ASSERT_EQUAL_UINT8(sequence, lastAckSequence);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Busy),
            static_cast<int>(lastAckResult));
    }

    void testApplicationSequenceNumbersWrapAfterAllByteValues()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x10);

        for (uint16_t value = 0; value <= 0xFFu; ++value) {
            TEST_ASSERT_TRUE(link.sendData(0x22, payload));
            const size_t frame = tinylink_test::writtenFrameCount(adapter) - 1;
            uint8_t type = 0;
            uint8_t sequence = 0;
            uint8_t bytes[sizeof(Payload)] = {};
            size_t length = 0;
            TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
                adapter, frame, type, sequence, bytes, sizeof(bytes), length));
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(value), sequence);
            TEST_ASSERT_TRUE(tinylink_test::queueAck(
                adapter, sequence, tinylink::AckResult::Ok));
            link.update();
        }

        TEST_ASSERT_TRUE(link.sendData(0x22, payload));
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t bytes[sizeof(Payload)] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter,
            tinylink_test::writtenFrameCount(adapter) - 1,
            type, sequence, bytes, sizeof(bytes), length));
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
    }

    void testReservedApplicationTypesAreRejectedWithoutConsumingSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x41);
        const uint8_t reserved[] = {
            tinylink::message_type_to_wire(tinylink::MessageType::Log),
            tinylink::message_type_to_wire(tinylink::MessageType::Ack),
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake)
        };

        for (size_t i = 0; i < sizeof(reserved); ++i) {
            TEST_ASSERT_FALSE(link.sendData(reserved[i], payload));
        }
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));

        TEST_ASSERT_TRUE(link.sendData(0x20, payload));
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t bytes[sizeof(Payload)] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, bytes, sizeof(bytes), length));
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
    }

    void testWrongUnsolicitedAndStaleAcksAreIgnoredAndCounted()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        link.onAckReceived(onAck);
        const Payload payload = payloadWith(0x51);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 44, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());

        TEST_ASSERT_TRUE(link.sendData(0x23, payload));
        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 1, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(0, ackCallbackCount);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(1, ackCallbackCount);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, ackCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(3, link.stats().unexpectedSequences);
    }

    void testUnknownAckResultIsAValidCorrelatedAcknowledgement()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        link.onAckReceived(onAck);
        const Payload payload = payloadWith(0x59);

        TEST_ASSERT_TRUE(link.sendData(0x23, payload));
        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Unknown));
        link.update();

        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(1, ackCallbackCount);
        TEST_ASSERT_EQUAL_UINT8(0, lastAckSequence);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Unknown),
            static_cast<int>(lastAckResult));
    }

    void testMalformedAckPayloadDoesNotReleasePendingTransmission()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x61);
        TEST_ASSERT_TRUE(link.sendData(0x24, payload));

        const uint8_t invalidResult[] = { 0x01u };
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Ack),
            0,
            invalidResult,
            sizeof(invalidResult)));
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Ack),
            0,
            nullptr,
            0));
        link.update();

        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(2, link.stats().invalidAckResults);
        TEST_ASSERT_EQUAL_UINT32(0, ackCallbackCount);
    }

    void testReceiveCallbackAndPollingMailboxExposeTheSamePayload()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        link.onDataReceived(onData);
        const Payload payload = payloadWith(0x71);

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x31, 99, payload.bytes, sizeof(payload.bytes)));
        link.update();

        TEST_ASSERT_EQUAL_UINT32(1, dataCallbackCount);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload.bytes, lastData.bytes, sizeof(payload.bytes));
        TEST_ASSERT_TRUE(link.available());
        const Payload received = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload.bytes, received.bytes, sizeof(payload.bytes));
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);
    }

    void testMailboxRetainsLatestPayloadUntilReadOrFlush()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload first = payloadWith(0x11);
        const Payload second = payloadWith(0x21);

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x32, 5, first.bytes, sizeof(first.bytes)));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x32, 6, second.bytes, sizeof(second.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        const Payload latest = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            second.bytes, latest.bytes, sizeof(second.bytes));
        TEST_ASSERT_FALSE(link.available());

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x32, 7, first.bytes, sizeof(first.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        link.flush();
        TEST_ASSERT_FALSE(link.available());
        const Payload retained = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            first.bytes, retained.bytes, sizeof(first.bytes));
    }

    void testApplicationFramesAreRejectedBeforeConnection()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const Payload payload = payloadWith(0x31);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x33, 0, payload.bytes, sizeof(payload.bytes)));
        link.update();

        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().unexpectedMessageTypes);
    }

    void testWrongApplicationPayloadSizeIsCountedWithoutMailboxUpdate()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const uint8_t shortPayload[] = { 1, 2, 3 };
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x34, 1, shortPayload, sizeof(shortPayload)));
        link.update();

        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().payloadSizeMismatches);
    }

    void testReceiveAndTransmitStatesRemainIndependent()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload sent = payloadWith(0x41);
        const Payload received = payloadWith(0x51);
        TEST_ASSERT_TRUE(link.sendData(0x25, sent));

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x35, 8, received.bytes, sizeof(received.bytes)));
        link.update();

        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_TRUE(link.available());
        const Payload result = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            received.bytes, result.bytes, sizeof(received.bytes));

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_TRUE(link.canSend());
    }

    void testValidLogFrameIsDecodedAndRoutedOnlyToLogCallback()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onLogReceived(onLog);
        tinylink::LogMessage message = {};
        message.uptimeMilliseconds = 0x12345678u;
        message.level = tinylink::LogLevel::WARN;
        message.code = 0xABCDu;
        tinylink::set_log_message_text(message, "transport warning");
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::encode_log_message(
                message, payload, sizeof(payload))));
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Log),
            77,
            payload,
            sizeof(payload)));
        link.update();

        TEST_ASSERT_EQUAL_UINT32(1, logCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(
            message.uptimeMilliseconds, lastLog.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(
            static_cast<int>(message.level), static_cast<int>(lastLog.level));
        TEST_ASSERT_EQUAL_UINT16(message.code, lastLog.code);
        TEST_ASSERT_EQUAL_STRING(message.text, lastLog.text);
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().packetsReceived);
    }

    void testMalformedLogPayloadIsCountedAndNotDelivered()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onLogReceived(onLog);
        uint8_t invalidLevel[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        invalidLevel[4] = 0xFFu;
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Log),
            0,
            invalidLevel,
            sizeof(invalidLevel)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, logCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().payloadSizeMismatches);
    }

    void testSendLogEncodesFieldsAndUsesTimeAndSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        adapter.setTime(0x12345678ul);
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::ERROR, 0xABCDu, "native log"));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Log), type);
        TEST_ASSERT_EQUAL_UINT8(0, sequence);

        tinylink::LogMessage message = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_UINT32(0x12345678ul, message.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogLevel::ERROR),
            static_cast<int>(message.level));
        TEST_ASSERT_EQUAL_UINT16(0xABCDu, message.code);
        TEST_ASSERT_EQUAL_STRING("native log", message.text);

        const Payload application = payloadWith(0x61);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(link.sendData(0x26, application));
        uint8_t appType = 0;
        uint8_t appSequence = 0;
        uint8_t appBytes[sizeof(Payload)] = {};
        size_t appLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter,
            tinylink_test::writtenFrameCount(adapter) - 1,
            appType, appSequence, appBytes, sizeof(appBytes), appLength));
        TEST_ASSERT_EQUAL_UINT8(1, appSequence);
    }

    void testSendLogTruncatesLongTextAndNullTextBecomesEmpty()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const char longText[] = "123456789012345678901234567890";
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::INFO, 1, longText));
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::INFO, 2, nullptr));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        tinylink::LogMessage message = {};
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_STRING("123456789012345678901234", message.text);

        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 1, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_STRING("", message.text);
    }

    void testSendAckUsesLatestReceivedSequenceAndResult()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x71);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 201, payload.bytes, sizeof(payload.bytes)));
        link.update();
        adapter.clearOutput();

        TEST_ASSERT_TRUE(link.sendAck(tinylink::AckResult::Busy));
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t ackPayload[tinylink::ACK_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, ackPayload, sizeof(ackPayload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Ack), type);
        TEST_ASSERT_EQUAL_UINT8(201, sequence);
        tinylink::AckResult result = tinylink::AckResult::Unknown;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_ack_message(
                ackPayload, payloadLength, result)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Busy),
            static_cast<int>(result));

        adapter.clearOutput();
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, ackPayload, sizeof(ackPayload), payloadLength));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Ok),
            static_cast<int>(ackPayload[0]));
    }

    void testSendAckRequiresAndRetainsAcceptedApplicationSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_TRUE(connectPeer(adapter, link));

        const Payload received = payloadWith(0x61);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 73, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        const Payload readPayload = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            received.bytes, readPayload.bytes, sizeof(received.bytes));
        link.flush();

        const Payload outgoing = payloadWith(0x71);
        TEST_ASSERT_TRUE(link.sendData(0x38, outgoing));
        adapter.clearOutput();
        TEST_ASSERT_TRUE(link.sendAck());

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t ackPayload[tinylink::ACK_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, ackPayload, sizeof(ackPayload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Ack), type);
        TEST_ASSERT_EQUAL_UINT8(73, sequence);
    }

    void testSendAckSequenceSurvivesCallbackDelivery()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        ackOnCallbackLink = &link;
        link.onDataReceived(acknowledgeOnData);

        const Payload received = payloadWith(0x41);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 91, received.bytes, sizeof(received.bytes)));
        link.update();

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t ackPayload[tinylink::ACK_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 1, type, sequence, ackPayload, sizeof(ackPayload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(91, sequence);
    }

    void testSendAckSequenceIsInvalidatedByBeginAndAcceptedHandshake()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload received = payloadWith(0x51);

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 92, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_TRUE(link.begin());
        TEST_ASSERT_FALSE(link.sendAck());

        adapter.clearOutput();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 93, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.sendAck());
        adapter.clearOutput();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        TEST_ASSERT_FALSE(link.sendAck());
    }

    void testEnginePacketTelemetrySaturates()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));

        tinylink::Stats stats = link.stats();
        stats.packetsReceived = UINT32_MAX - 1u;
        tinylink::TinyLinkTestAccess::setStats(link, stats);

        const Payload received = payloadWith(0x31);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 94, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, link.stats().packetsReceived);

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x37, 95, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, link.stats().packetsReceived);
    }

    void testMalformedCobsAndEmptyDelimitersAreHandled()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const uint8_t malformed[] = { 0x04u, 0x11u };
        TEST_ASSERT_TRUE(adapter.queueByte(0));
        TEST_ASSERT_TRUE(adapter.queueBytes(malformed, sizeof(malformed)));
        TEST_ASSERT_TRUE(adapter.queueByte(0));
        link.update();

        TEST_ASSERT_EQUAL_UINT32(1, link.stats().cobsDecodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().packetLengthMismatches);
        TEST_ASSERT_FALSE(link.available());
    }

    void testInvalidPacketChecksumIsCounted()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const uint8_t payload[] = { 1, 2, 3, 4 };
        uint8_t plain[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packet =
            tinylink::packet::pack(0x20, 1, payload, sizeof(payload),
                plain, sizeof(plain));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packet.status));
        plain[packet.length - 1] ^= 0x01u;
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();

        TEST_ASSERT_TRUE(tinylink_test::queueFrame(
            adapter, plain, packet.length));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().checksumMismatches);
        TEST_ASSERT_FALSE(link.available());
    }

    void testShortAndHeaderLengthMismatchPacketsAreCounted()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const uint8_t shortPlain[] = { 1, 2, 3 };
        TEST_ASSERT_TRUE(tinylink_test::queueFrame(
            adapter, shortPlain, sizeof(shortPlain)));

        const uint8_t payload[] = { 0x44u };
        uint8_t mismatched[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packed =
            tinylink::packet::pack(
                0x20, 1, payload, sizeof(payload), mismatched, sizeof(mismatched));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));
        mismatched[2] = 2;
        TEST_ASSERT_TRUE(tinylink_test::queueFrame(
            adapter, mismatched, packed.length));
        link.update();

        TEST_ASSERT_EQUAL_UINT32(2, link.stats().packetLengthMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().checksumMismatches);
    }

    void testInterByteTimeoutDropsPartialFrameAndRecovers()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.setTimeout(20);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x21);
        uint8_t encoded[tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u] = {};
        size_t encodedLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::encodePacket(
            0x28, 9, payload.bytes, sizeof(payload.bytes),
            encoded, sizeof(encoded), encodedLength));
        TEST_ASSERT_TRUE(encodedLength > 1);

        TEST_ASSERT_TRUE(adapter.queueBytes(encoded, encodedLength - 1));
        link.update();
        adapter.advanceTime(19);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().interByteTimeouts);

        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().interByteTimeouts);
        TEST_ASSERT_FALSE(link.available());

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x28, 10, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);
    }

    void testAckTimeoutReleasesTransmitPathExactlyAtDeadline()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.setTimeout(50);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x31);
        TEST_ASSERT_TRUE(link.sendData(0x29, payload));

        adapter.advanceTime(49);
        link.update();
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().ackTimeouts);

        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_TRUE(link.canSend());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().ackTimeouts);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().ackTimeouts);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().unexpectedSequences);
    }

    void testAckAndHandshakeTimersHandleUnsignedLongWraparound()
    {
        FakeAdapter adapter;
        adapter.setTime(ULONG_MAX - 9ul);
        Link link(adapter);
        link.setTimeout(10);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x41);
        TEST_ASSERT_TRUE(link.sendData(0x2A, payload));

        adapter.advanceTime(9);
        link.update();
        TEST_ASSERT_TRUE(link.awaitingAck());
        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().ackTimeouts);

        FakeAdapter handshakeAdapter;
        handshakeAdapter.setTime(ULONG_MAX - 500ul);
        Link handshake(handshakeAdapter);
        handshake.begin();
        handshakeAdapter.advanceTime(999);
        handshake.update();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(handshakeAdapter));
        handshakeAdapter.advanceTime(1);
        handshake.update();
        TEST_ASSERT_EQUAL_UINT32(2, tinylink_test::writtenFrameCount(handshakeAdapter));
    }

    void testReceiveOverflowDiscardsThroughDelimiterThenRecovers()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        uint8_t fullReceiveBuffer[
            tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u];
        memset(fullReceiveBuffer, 0x01, sizeof(fullReceiveBuffer));
        TEST_ASSERT_TRUE(adapter.queueBytes(
            fullReceiveBuffer, sizeof(fullReceiveBuffer)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().receiveOverflows);

        TEST_ASSERT_TRUE(adapter.queueByte(0x01u));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().receiveOverflows);

        const Payload payload = payloadWith(0x51);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x2B, 15, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_FALSE(link.available());

        TEST_ASSERT_TRUE(adapter.queueByte(0));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x2B, 16, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);
    }

    void testMaximumSupportedApplicationPayloadCanBeSentAndReceived()
    {
        FakeAdapter adapter;
        tinylink::TinyLink<MaximumPayload, FakeAdapter> link(adapter);
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        adapter.clearOutput();

        MaximumPayload payload = {};
        for (size_t i = 0; i < sizeof(payload.bytes); ++i) {
            payload.bytes[i] = static_cast<uint8_t>(i * 3u);
        }
        TEST_ASSERT_TRUE(link.sendData(0x2C, payload));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t decoded[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, decoded, sizeof(decoded), length));
        TEST_ASSERT_EQUAL_UINT32(sizeof(payload), length);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.bytes, decoded, sizeof(payload.bytes));

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, sequence, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x2C, 33, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        const MaximumPayload received = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload.bytes, received.bytes, sizeof(payload.bytes));
    }

    void testClosedTransportRejectsWritesAndDoesNotBlockRecovery()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x61);
        adapter.setOpen(false);
        TEST_ASSERT_FALSE(link.sendData(0x2D, payload));
        TEST_ASSERT_FALSE(link.sendLog(tinylink::LogLevel::INFO, 1, "closed"));
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_TRUE(link.canSend());

        adapter.setOpen(true);
        TEST_ASSERT_TRUE(link.sendData(0x2D, payload));
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t bytes[sizeof(Payload)] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, bytes, sizeof(bytes), length));
        TEST_ASSERT_EQUAL_UINT8(0x2D, type);
        TEST_ASSERT_TRUE(link.awaitingAck());
    }

    void testBeginWhileTransportClosedRetriesOnceAvailable()
    {
        FakeAdapter adapter;
        adapter.setOpen(false);
        Link link(adapter);
        link.begin();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));

        adapter.setOpen(true);
        adapter.advanceTime(1000);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));
    }

    void testResetClearsRuntimeStateButPreservesCallbacksTimeoutAndStats()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.setTimeout(30);
        link.onDataReceived(onData);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload received = payloadWith(0x71);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x38, 55, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);
        link.reset();

        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);

        link.begin();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(link.connected());

        const Payload outbound = payloadWith(0x11);
        TEST_ASSERT_TRUE(link.sendData(0x39, outbound));
        adapter.advanceTime(29);
        link.update();
        TEST_ASSERT_TRUE(link.awaitingAck());
        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().ackTimeouts);

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x38, 56, received.bytes, sizeof(received.bytes)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(2, dataCallbackCount);
    }

    void testResetCancelsPendingAckAndDiscardsPartialReceiveFrame()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        link.setTimeout(25);
        const Payload payload = payloadWith(0x31);
        TEST_ASSERT_TRUE(link.sendData(0x3B, payload));
        TEST_ASSERT_TRUE(link.awaitingAck());

        uint8_t encoded[tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u] = {};
        size_t encodedLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::encodePacket(
            0x3C, 7, payload.bytes, sizeof(payload.bytes),
            encoded, sizeof(encoded), encodedLength));
        TEST_ASSERT_TRUE(adapter.queueBytes(encoded, encodedLength));
        link.update();

        link.reset();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.canSend());
        adapter.advanceTime(100);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().ackTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().interByteTimeouts);

        link.begin();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(link.connected());
        adapter.clearOutput();
        TEST_ASSERT_TRUE(link.sendData(0x3B, payload));
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t sent[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t sentLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, sent, sizeof(sent), sentLength));
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
    }

    void testClearStatsResetsCountersWithoutChangingConnectionState()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const Payload payload = payloadWith(0x21);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x3A, 3, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().packetsReceived);
        link.clearStats();

        const tinylink::Stats stats = link.stats();
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(0, stats.cobsDecodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.checksumMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetLengthMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.payloadSizeMismatches);
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetEncodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.cobsEncodeFailures);
        TEST_ASSERT_EQUAL_UINT32(0, stats.interByteTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, stats.ackTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, stats.receiveOverflows);
        TEST_ASSERT_EQUAL_UINT32(0, stats.unexpectedMessageTypes);
        TEST_ASSERT_EQUAL_UINT32(0, stats.unexpectedSequences);
        TEST_ASSERT_EQUAL_UINT32(0, stats.invalidAckResults);
        TEST_ASSERT_EQUAL_UINT32(0, stats.invalidHandshakes);
        TEST_ASSERT_TRUE(link.connected());
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testConstructionStartsInactiveAndReadyWithZeroTelemetry);
    RUN_TEST(testBeginSendsVersionedHandshakeRequestAndEnablesConnectingState);
    RUN_TEST(testHandshakeRequestConnectsAndSendsCompatibleResponse);
    RUN_TEST(testHandshakeResponseConnectsOnlyAnInitiatingLink);
    RUN_TEST(testHandshakeRetriesAtTheConfiguredProtocolInterval);
    RUN_TEST(testUnsupportedHandshakeVersionMarksLinkIncompatible);
    RUN_TEST(testMalformedHandshakeDoesNotConnectOrInvokeCallback);
    RUN_TEST(testApplicationSendUsesOpaquePayloadAndCorrelatedAcknowledgement);
    RUN_TEST(testApplicationSequenceNumbersWrapAfterAllByteValues);
    RUN_TEST(testReservedApplicationTypesAreRejectedWithoutConsumingSequence);
    RUN_TEST(testWrongUnsolicitedAndStaleAcksAreIgnoredAndCounted);
    RUN_TEST(testUnknownAckResultIsAValidCorrelatedAcknowledgement);
    RUN_TEST(testMalformedAckPayloadDoesNotReleasePendingTransmission);
    RUN_TEST(testReceiveCallbackAndPollingMailboxExposeTheSamePayload);
    RUN_TEST(testMailboxRetainsLatestPayloadUntilReadOrFlush);
    RUN_TEST(testApplicationFramesAreRejectedBeforeConnection);
    RUN_TEST(testWrongApplicationPayloadSizeIsCountedWithoutMailboxUpdate);
    RUN_TEST(testReceiveAndTransmitStatesRemainIndependent);
    RUN_TEST(testValidLogFrameIsDecodedAndRoutedOnlyToLogCallback);
    RUN_TEST(testMalformedLogPayloadIsCountedAndNotDelivered);
    RUN_TEST(testSendLogEncodesFieldsAndUsesTimeAndSequence);
    RUN_TEST(testSendLogTruncatesLongTextAndNullTextBecomesEmpty);
    RUN_TEST(testSendAckUsesLatestReceivedSequenceAndResult);
    RUN_TEST(testSendAckRequiresAndRetainsAcceptedApplicationSequence);
    RUN_TEST(testSendAckSequenceSurvivesCallbackDelivery);
    RUN_TEST(testSendAckSequenceIsInvalidatedByBeginAndAcceptedHandshake);
    RUN_TEST(testEnginePacketTelemetrySaturates);
    RUN_TEST(testMalformedCobsAndEmptyDelimitersAreHandled);
    RUN_TEST(testInvalidPacketChecksumIsCounted);
    RUN_TEST(testShortAndHeaderLengthMismatchPacketsAreCounted);
    RUN_TEST(testInterByteTimeoutDropsPartialFrameAndRecovers);
    RUN_TEST(testAckTimeoutReleasesTransmitPathExactlyAtDeadline);
    RUN_TEST(testAckAndHandshakeTimersHandleUnsignedLongWraparound);
    RUN_TEST(testReceiveOverflowDiscardsThroughDelimiterThenRecovers);
    RUN_TEST(testMaximumSupportedApplicationPayloadCanBeSentAndReceived);
    RUN_TEST(testClosedTransportRejectsWritesAndDoesNotBlockRecovery);
    RUN_TEST(testBeginWhileTransportClosedRetriesOnceAvailable);
    RUN_TEST(testResetClearsRuntimeStateButPreservesCallbacksTimeoutAndStats);
    RUN_TEST(testResetCancelsPendingAckAndDiscardsPartialReceiveFrame);
    RUN_TEST(testClearStatsResetsCountersWithoutChangingConnectionState);
    return UNITY_END();
}

extern "C" void setUp(void)
{
    dataCallbackCount = 0;
    logCallbackCount = 0;
    handshakeCallbackCount = 0;
    ackCallbackCount = 0;
    memset(&lastData, 0, sizeof(lastData));
    memset(&lastLog, 0, sizeof(lastLog));
    lastHandshake.kind = tinylink::HandshakeKind::Request;
    lastHandshake.protocolVersion = 0;
    lastAckSequence = 0;
    lastAckResult = tinylink::AckResult::Unknown;
}

extern "C" void tearDown(void)
{
}
