#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

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

void testEngineCountersSaturateAtMaximumInsteadOfWrapping()
    {
        FakeAdapter adapter;
        Link link(adapter);
        tinylink::Stats& counters =
            tinylink::TinyLinkStatsAccess::stats(link);
        counters.packetsReceived = UINT32_MAX - 1u;
        const Payload payload = payloadWith(0x30);
        const size_t size = sizeof(payload.bytes);

        // Rejected before connection: exercises UnexpectedMessageType.
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 1, payload.bytes, size));
        counters.unexpectedMessageTypes = UINT32_MAX;
        link.update();
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, link.stats().unexpectedMessageTypes);

        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 2, payload.bytes, size));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 3, payload.bytes, size));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 4, payload.bytes, size));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, link.stats().packetsReceived);

        counters.cobsDecodeFailures = UINT32_MAX;
        counters.checksumMismatches = UINT32_MAX;
        counters.packetLengthMismatches = UINT32_MAX;
        counters.payloadSizeMismatches = UINT32_MAX;
        counters.invalidAckResults = UINT32_MAX;
        counters.unexpectedSequences = UINT32_MAX;
        counters.invalidHandshakes = UINT32_MAX;
        counters.receiveOverflows = UINT32_MAX;
        counters.interByteTimeouts = UINT32_MAX;
        counters.ackTimeouts = UINT32_MAX;

        const uint8_t malformed[] = { 0x04u, 0x11u, 0x00u };
        TEST_ASSERT_TRUE(adapter.queueBytes(malformed, sizeof(malformed)));

        uint8_t plain[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packed = tinylink::packet::pack(
            0x20, 1, payload.bytes, size, plain, sizeof(plain));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));
        plain[packed.length - 1] ^= 0x01u;
        TEST_ASSERT_TRUE(tinylink_test::queueFrame(adapter, plain, packed.length));

        const uint8_t shortPlain[] = { 1, 2, 3 };
        TEST_ASSERT_TRUE(tinylink_test::queueFrame(
            adapter, shortPlain, sizeof(shortPlain)));

        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter, 0x20, 5, payload.bytes, size - 1));
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Ack),
            0, nullptr, 0));
        TEST_ASSERT_TRUE(tinylink_test::queueAck(
            adapter, 9, tinylink::AckResult::Ok));
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0, nullptr, 0));

        uint8_t overflow[
            tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 2u];
        memset(overflow, 0x01, sizeof(overflow));
        TEST_ASSERT_TRUE(adapter.queueBytes(overflow, sizeof(overflow)));
        TEST_ASSERT_TRUE(adapter.queueByte(0));
        link.update();

        TEST_ASSERT_TRUE(adapter.queueByte(0x01u));
        link.update();
        adapter.advanceTime(1000);
        link.update();

        TEST_ASSERT_TRUE(link.sendData(0x2A, payload));
        adapter.advanceTime(1000);
        link.update();

        const tinylink::Stats stats = link.stats();
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.cobsDecodeFailures);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.checksumMismatches);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.packetLengthMismatches);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.payloadSizeMismatches);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.invalidAckResults);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.unexpectedSequences);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.invalidHandshakes);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.receiveOverflows);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.interByteTimeouts);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.ackTimeouts);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.unexpectedMessageTypes);
        TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, stats.packetsReceived);
    }

} // namespace

void registerTelemetryTests()
{
    RUN_TEST(testClearStatsResetsCountersWithoutChangingConnectionState);
    RUN_TEST(testEngineCountersSaturateAtMaximumInsteadOfWrapping);
}
