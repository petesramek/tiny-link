#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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

void testTransportLossPreservesDeliveredPollingPayload()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));

        const Payload payload = payloadWith(0x71);
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x43, 76, payload.bytes, sizeof(payload.bytes)));
        link.update();
        TEST_ASSERT_TRUE(link.available());

        adapter.setOpen(false);
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Failed),
            static_cast<int>(link.connectionState()));
        link.update();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Failed),
            static_cast<int>(link.connectionState()));
        TEST_ASSERT_TRUE(link.available());

        const Payload received = link.read();
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            payload.bytes, received.bytes, sizeof(payload.bytes));
    }

} // namespace

void registerReceiveMailboxTests()
{
    RUN_TEST(testReceiveCallbackAndPollingMailboxExposeTheSamePayload);
    RUN_TEST(testMailboxRetainsLatestPayloadUntilReadOrFlush);
    RUN_TEST(testApplicationFramesAreRejectedBeforeConnection);
    RUN_TEST(testWrongApplicationPayloadSizeIsCountedWithoutMailboxUpdate);
    RUN_TEST(testReceiveAndTransmitStatesRemainIndependent);
    RUN_TEST(testTransportLossPreservesDeliveredPollingPayload);
}
