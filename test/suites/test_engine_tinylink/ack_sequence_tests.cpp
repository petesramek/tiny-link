#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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
            const char* context = tinylink_test::diag(
                "sequence wrap iteration=%u (expected sequence 0x%02X)",
                (unsigned)value, (unsigned)(value & 0xFFu));
            TEST_ASSERT_TRUE_MESSAGE(link.sendData(0x22, payload), context);
            const size_t frame = tinylink_test::writtenFrameCount(adapter) - 1;
            uint8_t type = 0;
            uint8_t sequence = 0;
            uint8_t bytes[sizeof(Payload)] = {};
            size_t length = 0;
            TEST_ASSERT_TRUE_MESSAGE(tinylink_test::decodeWrittenFrame(
                adapter, frame, type, sequence, bytes, sizeof(bytes), length),
                context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                static_cast<uint8_t>(value), sequence, context);
            TEST_ASSERT_TRUE_MESSAGE(tinylink_test::queueAck(
                adapter, sequence, tinylink::AckResult::Ok), context);
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

void testSendAckPropagatesFailedAndPartialWrites()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 5));

        const size_t failed[] = { 0 };
        scriptWrites(adapter, failed, 1);
        TEST_ASSERT_FALSE(link.sendAck());

        const size_t partial[] = { 2 };
        scriptWrites(adapter, partial, 1);
        TEST_ASSERT_FALSE(link.sendAck());

        const size_t noDelimiter[] = { ALL, 0 };
        adapter.clearOutput();
        scriptWrites(adapter, noDelimiter, 2);
        TEST_ASSERT_FALSE(link.sendAck());

        adapter.clearOutput();
        TEST_ASSERT_TRUE(link.sendAck());
    }

void testSendAckReturnsFalseBeforeAnyApplicationFrameIsAccepted()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());

        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testRejectedApplicationFramesDoNotValidateAckSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const Payload payload = payloadWith(0x40);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 7, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_FALSE(link.sendAck());

        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 8, payload.bytes, sizeof(payload.bytes) - 1));
        link.update();
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());
    }

void testAckSequenceRemainsValidAfterCallbackDelivery()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onDataReceived(onData);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 41));

        TEST_ASSERT_EQUAL_UINT32(1, dataCallbackCount);
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT8(41, ackedSequence(adapter));
    }

void testAckSequenceRemainsValidAfterRead()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 42));

        static_cast<void>(link.read());
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT8(42, ackedSequence(adapter));
    }

void testAckSequenceRemainsValidAfterFlush()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 43));

        link.flush();
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_TRUE(link.sendAck());
        TEST_ASSERT_EQUAL_UINT8(43, ackedSequence(adapter));
    }

} // namespace

void registerAckSequenceTests()
{
    RUN_TEST(testApplicationSendUsesOpaquePayloadAndCorrelatedAcknowledgement);
    RUN_TEST(testApplicationSequenceNumbersWrapAfterAllByteValues);
    RUN_TEST(testWrongUnsolicitedAndStaleAcksAreIgnoredAndCounted);
    RUN_TEST(testUnknownAckResultIsAValidCorrelatedAcknowledgement);
    RUN_TEST(testMalformedAckPayloadDoesNotReleasePendingTransmission);
    RUN_TEST(testSendAckUsesLatestReceivedSequenceAndResult);
    RUN_TEST(testSendAckPropagatesFailedAndPartialWrites);
    RUN_TEST(testSendAckReturnsFalseBeforeAnyApplicationFrameIsAccepted);
    RUN_TEST(testRejectedApplicationFramesDoNotValidateAckSequence);
    RUN_TEST(testAckSequenceRemainsValidAfterCallbackDelivery);
    RUN_TEST(testAckSequenceRemainsValidAfterRead);
    RUN_TEST(testAckSequenceRemainsValidAfterFlush);
}
