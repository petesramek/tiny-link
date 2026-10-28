#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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

void testBeginReportsWriteResultAndKeepsConnectingForRetry()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const size_t failed[] = { 0 };
        scriptWrites(adapter, failed, 1);
        TEST_ASSERT_FALSE(link.begin());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));

        adapter.advanceTime(1000);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        FakeAdapter okAdapter;
        Link okLink(okAdapter);
        TEST_ASSERT_TRUE(okLink.begin());
    }

void testHandshakeRequestConnectsOnlyAfterResponseIsWritten()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onHandshakeReceived(onHandshake);
        const size_t partial[] = { 2 };
        scriptWrites(adapter, partial, 1);
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(link.connectionState()));

        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        TEST_ASSERT_TRUE(link.connected());
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

void testResetInvalidatesAckSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 44));
        TEST_ASSERT_TRUE(link.sendAck());
        adapter.clearOutput();

        link.reset();
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testBeginInvalidatesAckSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 45));

        TEST_ASSERT_TRUE(link.begin());
        adapter.clearOutput();
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testPeerHandshakeRequestInvalidatesAckSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 46));

        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        adapter.clearOutput();
        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testIncompatibleHandshakeInvalidatesAckSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 47));

        const tinylink::HandshakeMessage supported = {
            tinylink::HandshakeKind::Request,
            TINYLINK_PROTOCOL_VERSION
        };
        uint8_t wire[tinylink::HANDSHAKE_MESSAGE_WIRE_SIZE] = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::Success),
            static_cast<int>(tinylink::encode_handshake_message(
                supported, wire, sizeof(wire))));
        wire[sizeof(wire) - 1] = static_cast<uint8_t>(
            TINYLINK_PROTOCOL_VERSION + 1);
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0, wire, sizeof(wire)));
        link.update();

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Incompatible),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testTransportLossCancelsAcksAndRestartsHandshakeWithoutSequenceReuse()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onAckReceived(onAck);
        ackCallbackCount = 0;
        TEST_ASSERT_TRUE(link.begin());
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(link.connected());

        const Payload outbound = payloadWith(0x31);
        TEST_ASSERT_TRUE(link.sendData(0x40, outbound));
        TEST_ASSERT_TRUE(link.awaitingAck());
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t bytes[sizeof(Payload)] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 1, type, sequence, bytes, sizeof(bytes), length));
        TEST_ASSERT_EQUAL_UINT8(0, sequence);
        adapter.clearOutput();

        const Payload inbound = payloadWith(0x51);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x44, 73, inbound.bytes, sizeof(inbound.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT8(73, ackedSequence(adapter));

        adapter.setOpen(false);
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Failed),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_FALSE(link.canSend());
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, ackCallbackCount);

        adapter.clearOutput();
        const size_t failedHandshakeWrite[] = { 0 };
        scriptWrites(adapter, failedHandshakeWrite, 1);
        adapter.setOpen(true);
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));

        adapter.advanceTime(999);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));
        adapter.advanceTime(1);
        link.update();
        TEST_ASSERT_EQUAL_UINT32(1, tinylink_test::writtenFrameCount(adapter));

        uint8_t handshakePayload[tinylink::HANDSHAKE_MESSAGE_WIRE_SIZE] = {};
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, handshakePayload,
            sizeof(handshakePayload), length));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            type);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Request),
            static_cast<int>(handshakePayload[0]));

        adapter.clearOutput();
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Response));
        link.update();
        TEST_ASSERT_TRUE(link.connected());
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_TRUE(link.sendData(0x40, outbound));
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, bytes, sizeof(bytes), length));
        TEST_ASSERT_EQUAL_UINT8(1, sequence);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 0, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(0, ackCallbackCount);

        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 1, tinylink::AckResult::Ok));
        link.update();
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(1, ackCallbackCount);
        TEST_ASSERT_EQUAL_UINT8(1, lastAckSequence);
    }

void testTransportLossDiscardsPartialFrameAndRestoresBasicLinkForSynchronization()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));

        const Payload payload = payloadWith(0x61);
        uint8_t encoded[tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u] = {};
        size_t encodedLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::encodePacket(
            0x42, 74, payload.bytes, sizeof(payload.bytes),
            encoded, sizeof(encoded), encodedLength));
        TEST_ASSERT_TRUE(encodedLength > 1);
        TEST_ASSERT_TRUE(adapter.queueBytes(encoded, encodedLength / 2));
        link.update();
        TEST_ASSERT_FALSE(link.available());

        adapter.setOpen(false);
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Failed),
            static_cast<int>(link.connectionState()));

        adapter.setOpen(true);
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_FALSE(link.canSend());

        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter, tinylink::HandshakeKind::Request));
        link.update();
        TEST_ASSERT_TRUE(link.connected());

        TEST_ASSERT_TRUE(adapter.queueBytes(
            encoded + encodedLength / 2,
            encodedLength - encodedLength / 2));
        TEST_ASSERT_TRUE(adapter.queueByte(0));
        link.update();
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x42, 75, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());
        const Payload received = link.read();
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload.bytes, received.bytes, sizeof(payload.bytes));
    }

} // namespace

void registerLifecycleTests()
{
    RUN_TEST(testConstructionStartsInactiveAndReadyWithZeroTelemetry);
    RUN_TEST(testBeginSendsVersionedHandshakeRequestAndEnablesConnectingState);
    RUN_TEST(testHandshakeRequestConnectsAndSendsCompatibleResponse);
    RUN_TEST(testHandshakeResponseConnectsOnlyAnInitiatingLink);
    RUN_TEST(testUnsupportedHandshakeVersionMarksLinkIncompatible);
    RUN_TEST(testMalformedHandshakeDoesNotConnectOrInvokeCallback);
    RUN_TEST(testBeginReportsWriteResultAndKeepsConnectingForRetry);
    RUN_TEST(testHandshakeRequestConnectsOnlyAfterResponseIsWritten);
    RUN_TEST(testBeginWhileTransportClosedRetriesOnceAvailable);
    RUN_TEST(testResetClearsRuntimeStateButPreservesCallbacksTimeoutAndStats);
    RUN_TEST(testResetCancelsPendingAckAndDiscardsPartialReceiveFrame);
    RUN_TEST(testResetInvalidatesAckSequence);
    RUN_TEST(testBeginInvalidatesAckSequence);
    RUN_TEST(testPeerHandshakeRequestInvalidatesAckSequence);
    RUN_TEST(testIncompatibleHandshakeInvalidatesAckSequence);
    RUN_TEST(testTransportLossCancelsAcksAndRestartsHandshakeWithoutSequenceReuse);
    RUN_TEST(testTransportLossDiscardsPartialFrameAndRestoresBasicLinkForSynchronization);
}
