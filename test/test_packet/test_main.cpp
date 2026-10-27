#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "internal/codec/Packet.h"

namespace {

    void testPacketSizeCoversEmptyMaximumAndTooLarge()
    {
        size_t size = 999;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(tinylink::packet::packet_size(0, size)));
        TEST_ASSERT_EQUAL_UINT32(tinylink::packet::PACKET_MIN_SIZE, size);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(tinylink::packet::packet_size(
                tinylink::packet::PACKET_MAX_PAYLOAD_SIZE, size)));
        TEST_ASSERT_EQUAL_UINT32(tinylink::packet::PACKET_MAX_SIZE, size);

        size = 77;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::PayloadTooLarge),
            static_cast<int>(tinylink::packet::packet_size(
                tinylink::packet::PACKET_MAX_PAYLOAD_SIZE + 1, size)));
        TEST_ASSERT_EQUAL_UINT32(77, size);
    }

    void testEmptyPacketHasDocumentedHeaderAndChecksumByteOrder()
    {
        uint8_t packet[tinylink::packet::PACKET_MIN_SIZE] = {};
        const tinylink::packet::PacketResult result =
            tinylink::packet::pack(
                0xE7u, 0xA5u, nullptr, 0, packet, sizeof(packet));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(sizeof(packet), result.length);
        TEST_ASSERT_EQUAL_UINT8(0xE7u, packet[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA5u, packet[1]);
        TEST_ASSERT_EQUAL_UINT8(0, packet[2]);
        TEST_ASSERT_EQUAL_UINT8(0x8Du, packet[3]);
        TEST_ASSERT_EQUAL_UINT8(0x03u, packet[4]);
    }

    void testPacketRoundTripsEverySupportedPayloadLength()
    {
        uint8_t payload[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE];
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE];
        uint8_t decoded[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE];

        for (size_t i = 0; i < sizeof(payload); ++i) {
            payload[i] = static_cast<uint8_t>(i * 37u + 0xA3u);
        }

        for (size_t length = 0; length <= sizeof(payload); ++length) {
            const uint8_t* input = length == 0 ? nullptr : payload;
            const tinylink::packet::PacketResult packed =
                tinylink::packet::pack(
                    0xF2u, 0xD6u, input, length, packet, sizeof(packet));
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::packet::PacketStatus::Success),
                static_cast<int>(packed.status));
            TEST_ASSERT_EQUAL_UINT32(
                tinylink::packet::PACKET_HEADER_SIZE +
                    length + tinylink::packet::PACKET_CHECKSUM_SIZE,
                packed.length);

            uint8_t type = 0;
            uint8_t sequence = 0;
            size_t decodedLength = 999;
            const tinylink::packet::PacketResult unpacked =
                tinylink::packet::unpack(
                    packet, packed.length, &type, &sequence,
                    decoded, sizeof(decoded), &decodedLength);
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::packet::PacketStatus::Success),
                static_cast<int>(unpacked.status));
            TEST_ASSERT_EQUAL_UINT8(0xF2u, type);
            TEST_ASSERT_EQUAL_UINT8(0xD6u, sequence);
            TEST_ASSERT_EQUAL_UINT32(length, decodedLength);
            if (length > 0) {
                TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, decoded, length);
            }
        }
    }

    void testRawApplicationTypeAndSequenceArePreserved()
    {
        const uint8_t payload[] = { 0x00u, 0xFFu };
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE] = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(tinylink::packet::pack(
                0x00u, 0xFFu, payload, sizeof(payload),
                packet, sizeof(packet)).status));
        TEST_ASSERT_EQUAL_UINT8(0x00u, packet[0]);
        TEST_ASSERT_EQUAL_UINT8(0xFFu, packet[1]);

        uint8_t type = 0xAAu;
        uint8_t sequence = 0xBBu;
        size_t length = 0;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(tinylink::packet::unpack(
                packet, tinylink::packet::PACKET_HEADER_SIZE + sizeof(payload) +
                    tinylink::packet::PACKET_CHECKSUM_SIZE,
                &type, &sequence, nullptr, 0, &length).status));
        TEST_ASSERT_EQUAL_UINT8(0x00u, type);
        TEST_ASSERT_EQUAL_UINT8(0xFFu, sequence);
        TEST_ASSERT_EQUAL_UINT32(sizeof(payload), length);
    }

    void testUnpackValidatesWithoutCopyingWhenPayloadDestinationIsNull()
    {
        const uint8_t payload[] = { 0x17u, 0x00u, 0xE2u };
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packed =
            tinylink::packet::pack(
                0x31u, 0x42u, payload, sizeof(payload),
                packet, sizeof(packet));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));

        uint8_t type = 0;
        uint8_t sequence = 0;
        size_t length = 0;
        const tinylink::packet::PacketResult unpacked =
            tinylink::packet::unpack(
                packet, packed.length, &type, &sequence, nullptr, 0, &length);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(unpacked.status));
        TEST_ASSERT_EQUAL_UINT8(0x31u, type);
        TEST_ASSERT_EQUAL_UINT8(0x42u, sequence);
        TEST_ASSERT_EQUAL_UINT32(sizeof(payload), length);
    }

    void testPackRejectsNullAndCapacityErrorsWithoutChangingDestination()
    {
        const uint8_t payload = 0x55u;
        uint8_t output[8];
        const uint8_t original[8] = {
            0xA0u, 0xA1u, 0xA2u, 0xA3u, 0xA4u, 0xA5u, 0xA6u, 0xA7u
        };
        memcpy(output, original, sizeof(output));

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::InvalidArgument),
            static_cast<int>(tinylink::packet::pack(
                1, 2, &payload, 1, nullptr, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::InvalidArgument),
            static_cast<int>(tinylink::packet::pack(
                1, 2, nullptr, 1, output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::OutputTooSmall),
            static_cast<int>(tinylink::packet::pack(
                1, 2, &payload, 1, output, 5).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::PayloadTooLarge),
            static_cast<int>(tinylink::packet::pack(
                1, 2, &payload,
                tinylink::packet::PACKET_MAX_PAYLOAD_SIZE + 1,
                output, sizeof(output)).status));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original, output, sizeof(output));
    }

    void testUnpackRejectsNullShortOversizedAndLengthMismatchFrames()
    {
        const uint8_t shortPacket[] = { 1, 2, 3, 4 };
        uint8_t oversized[tinylink::packet::PACKET_MIN_SIZE] = {
            1, 2, 65, 0, 0
        };
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE] = {};
        const uint8_t oneByte[] = { 7 };
        const tinylink::packet::PacketResult packed =
            tinylink::packet::pack(1, 2, oneByte, 1, packet, sizeof(packet));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::InvalidArgument),
            static_cast<int>(tinylink::packet::unpack(
                nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::FrameTooShort),
            static_cast<int>(tinylink::packet::unpack(
                shortPacket, sizeof(shortPacket),
                nullptr, nullptr, nullptr, 0, nullptr).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::PayloadTooLarge),
            static_cast<int>(tinylink::packet::unpack(
                oversized, sizeof(oversized),
                nullptr, nullptr, nullptr, 0, nullptr).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::LengthMismatch),
            static_cast<int>(tinylink::packet::unpack(
                packet, packed.length - 1,
                nullptr, nullptr, nullptr, 0, nullptr).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::LengthMismatch),
            static_cast<int>(tinylink::packet::unpack(
                packet, packed.length + 1,
                nullptr, nullptr, nullptr, 0, nullptr).status));
    }

    void testUnpackFailuresPreserveAllOutputDestinations()
    {
        const uint8_t payload[] = { 0x51u, 0x52u };
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packed =
            tinylink::packet::pack(
                0x33u, 0x44u, payload, sizeof(payload),
                packet, sizeof(packet));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));
        packet[packed.length - 1] ^= 0x80u;

        uint8_t type = 0xA1u;
        uint8_t sequence = 0xA2u;
        uint8_t decoded[] = { 0xA3u, 0xA4u, 0xA5u };
        size_t length = 0xA6u;
        const tinylink::packet::PacketResult result =
            tinylink::packet::unpack(
                packet, packed.length, &type, &sequence,
                decoded, sizeof(decoded), &length);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::ChecksumMismatch),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT8(0xA1u, type);
        TEST_ASSERT_EQUAL_UINT8(0xA2u, sequence);
        TEST_ASSERT_EQUAL_UINT8(0xA3u, decoded[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA4u, decoded[1]);
        TEST_ASSERT_EQUAL_UINT8(0xA5u, decoded[2]);
        TEST_ASSERT_EQUAL_UINT32(0xA6u, length);

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::OutputTooSmall),
            static_cast<int>(tinylink::packet::unpack(
                packet, packed.length, &type, &sequence,
                decoded, 1, &length).status));
        TEST_ASSERT_EQUAL_UINT8(0xA1u, type);
        TEST_ASSERT_EQUAL_UINT8(0xA2u, sequence);
        TEST_ASSERT_EQUAL_UINT8(0xA3u, decoded[0]);
        TEST_ASSERT_EQUAL_UINT8(0xA4u, decoded[1]);
        TEST_ASSERT_EQUAL_UINT8(0xA5u, decoded[2]);
        TEST_ASSERT_EQUAL_UINT32(0xA6u, length);
    }

    void testChecksumProtectsHeaderAndPayloadAndUsesLowByteFirst()
    {
        const uint8_t payload[] = { 0x16u, 0x27u };
        uint8_t packet[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packed =
            tinylink::packet::pack(
                0x39u, 0x4Au, payload, sizeof(payload),
                packet, sizeof(packet));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::Success),
            static_cast<int>(packed.status));

        const size_t protectedByteOffsets[] = { 0, 1, 3, 4 };
        for (size_t i = 0;
            i < sizeof(protectedByteOffsets) / sizeof(protectedByteOffsets[0]);
            ++i) {
            uint8_t corrupted[tinylink::packet::PACKET_MAX_SIZE] = {};
            memcpy(corrupted, packet, packed.length);
            corrupted[protectedByteOffsets[i]] ^= 0x01u;
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::packet::PacketStatus::ChecksumMismatch),
                static_cast<int>(tinylink::packet::unpack(
                    corrupted, packed.length, nullptr, nullptr,
                    nullptr, 0, nullptr).status));
        }

        uint8_t corruptedChecksum[tinylink::packet::PACKET_MAX_SIZE] = {};
        memcpy(corruptedChecksum, packet, packed.length);
        corruptedChecksum[packed.length - 1] ^= 0x01u;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::ChecksumMismatch),
            static_cast<int>(tinylink::packet::unpack(
                corruptedChecksum, packed.length, nullptr, nullptr,
                nullptr, 0, nullptr).status));

        uint8_t wrongLength[tinylink::packet::PACKET_MAX_SIZE] = {};
        memcpy(wrongLength, packet, packed.length);
        wrongLength[2] = static_cast<uint8_t>(sizeof(payload) + 1);
        uint8_t type = 0xA1u;
        uint8_t sequence = 0xA2u;
        uint8_t decoded[] = { 0xA3u };
        size_t length = 0xA4u;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::packet::PacketStatus::LengthMismatch),
            static_cast<int>(tinylink::packet::unpack(
                wrongLength, packed.length, &type, &sequence,
                decoded, sizeof(decoded), &length).status));
        TEST_ASSERT_EQUAL_UINT8(0xA1u, type);
        TEST_ASSERT_EQUAL_UINT8(0xA2u, sequence);
        TEST_ASSERT_EQUAL_UINT8(0xA3u, decoded[0]);
        TEST_ASSERT_EQUAL_UINT32(0xA4u, length);
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testPacketSizeCoversEmptyMaximumAndTooLarge);
    RUN_TEST(testEmptyPacketHasDocumentedHeaderAndChecksumByteOrder);
    RUN_TEST(testPacketRoundTripsEverySupportedPayloadLength);
    RUN_TEST(testRawApplicationTypeAndSequenceArePreserved);
    RUN_TEST(testUnpackValidatesWithoutCopyingWhenPayloadDestinationIsNull);
    RUN_TEST(testPackRejectsNullAndCapacityErrorsWithoutChangingDestination);
    RUN_TEST(testUnpackRejectsNullShortOversizedAndLengthMismatchFrames);
    RUN_TEST(testUnpackFailuresPreserveAllOutputDestinations);
    RUN_TEST(testChecksumProtectsHeaderAndPayloadAndUsesLowByteFirst);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
