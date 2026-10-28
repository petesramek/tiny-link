#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

void testPayloadContractAcceptsValidAndMaximumPayloads()
    {
        FakeAdapter adapter;
        tinylink::TinyLink<Payload, FakeAdapter> small(adapter);
        tinylink::TinyLink<MaximumPayload, FakeAdapter> maximum(adapter);
        TEST_ASSERT_EQUAL_UINT32(4, sizeof(Payload));
        TEST_ASSERT_EQUAL_UINT32(tinylink::packet::PACKET_MAX_PAYLOAD_SIZE,
                                 sizeof(MaximumPayload));
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

} // namespace

void registerApplicationTests()
{
    RUN_TEST(testPayloadContractAcceptsValidAndMaximumPayloads);
    RUN_TEST(testReservedApplicationTypesAreRejectedWithoutConsumingSequence);
    RUN_TEST(testMaximumSupportedApplicationPayloadCanBeSentAndReceived);
}
