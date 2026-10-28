#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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

void testClosedTransportRejectsWritesAndDoesNotBlockRecovery()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(receiveApplication(adapter, link, 5));
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

void testSendDataFailedBodyWriteDoesNotAwaitAckOrConsumeSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const size_t script[] = { 0 };
        scriptWrites(adapter, script, 1);

        TEST_ASSERT_FALSE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_TRUE(link.canSend());
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());

        TEST_ASSERT_TRUE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_TRUE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter, 0));
    }

void testSendDataPartialBodyIsTerminatedBeforeNextFrameAndKeepsSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const size_t script[] = { 3 };
        scriptWrites(adapter, script, 1);

        TEST_ASSERT_FALSE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(3, adapter.outputSize());

        TEST_ASSERT_TRUE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_TRUE(link.awaitingAck());
        // Fragment, recovery delimiter, then the complete frame.
        TEST_ASSERT_EQUAL_UINT32(2, tinylink_test::writtenFrameCount(adapter));
        TEST_ASSERT_EQUAL_UINT8(0, adapter.output()[3]);
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter, 1));
    }

void testSendDataDelimiterFailureIsIncompleteAndRecovered()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const size_t script[] = { ALL, 0 };
        scriptWrites(adapter, script, 2);

        TEST_ASSERT_FALSE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(0, tinylink_test::writtenFrameCount(adapter));
        const size_t bodySize = adapter.outputSize();
        TEST_ASSERT_TRUE(bodySize > 0);

        TEST_ASSERT_TRUE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_EQUAL_UINT32(2, tinylink_test::writtenFrameCount(adapter));
        TEST_ASSERT_EQUAL_UINT8(0, adapter.output()[bodySize]);
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter, 1));
    }

void testFailedRecoveryDelimiterKeepsFragmentPendingAndFailsWrite()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        const size_t script[] = { 2, 0 };
        scriptWrites(adapter, script, 2);

        TEST_ASSERT_FALSE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_FALSE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT32(2, adapter.outputSize());

        TEST_ASSERT_TRUE(link.sendData(0x40, payloadWith(1)));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter, 1));
    }

} // namespace

void registerOverflowRecoveryTests()
{
    RUN_TEST(testMalformedCobsAndEmptyDelimitersAreHandled);
    RUN_TEST(testInvalidPacketChecksumIsCounted);
    RUN_TEST(testShortAndHeaderLengthMismatchPacketsAreCounted);
    RUN_TEST(testReceiveOverflowDiscardsThroughDelimiterThenRecovers);
    RUN_TEST(testClosedTransportRejectsWritesAndDoesNotBlockRecovery);
    RUN_TEST(testSendDataFailedBodyWriteDoesNotAwaitAckOrConsumeSequence);
    RUN_TEST(testSendDataPartialBodyIsTerminatedBeforeNextFrameAndKeepsSequence);
    RUN_TEST(testSendDataDelimiterFailureIsIncompleteAndRecovered);
    RUN_TEST(testFailedRecoveryDelimiterKeepsFragmentPendingAndFailsWrite);
}
