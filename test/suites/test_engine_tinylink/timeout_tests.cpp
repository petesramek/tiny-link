#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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

} // namespace

void registerTimeoutTests()
{
    RUN_TEST(testHandshakeRetriesAtTheConfiguredProtocolInterval);
    RUN_TEST(testInterByteTimeoutDropsPartialFrameAndRecovers);
    RUN_TEST(testAckTimeoutReleasesTransmitPathExactlyAtDeadline);
    RUN_TEST(testAckAndHandshakeTimersHandleUnsignedLongWraparound);
}
