#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "internal/codec/CobsCodec.h"

namespace {

    void testEncodedSizeIncludesExactBlockOverhead()
    {
        const size_t lengths[] = { 0, 1, 253, 254, 255, 508, 509 };
        const size_t expected[] = { 1, 2, 254, 256, 257, 511, 512 };

        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            size_t required = 999;
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(tinylink::codec::cobs_encoded_size(
                    lengths[i], required)));
            TEST_ASSERT_EQUAL_UINT32(expected[i], required);
        }
    }

    void testEncodedSizeReportsOverflowWithoutChangingOutput()
    {
        size_t required = 12345;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::SizeOverflow),
            static_cast<int>(tinylink::codec::cobs_encoded_size(
                static_cast<size_t>(-1), required)));
        TEST_ASSERT_EQUAL_UINT32(12345, required);
    }

    void testEmptyInputHasCanonicalOneByteEncoding()
    {
        uint8_t encoded[1] = { 0xA5u };
        const tinylink::codec::CobsResult result =
            tinylink::codec::cobs_encode(nullptr, 0, encoded, sizeof(encoded));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::Success),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(1, result.bytesWritten);
        TEST_ASSERT_EQUAL_UINT8(0x01u, encoded[0]);

        size_t decodedSize = 99;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::Success),
            static_cast<int>(tinylink::codec::cobs_decoded_size(
                encoded, sizeof(encoded), decodedSize)));
        TEST_ASSERT_EQUAL_UINT32(0, decodedSize);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::Success),
            static_cast<int>(tinylink::codec::cobs_decode(
                encoded, sizeof(encoded), nullptr, 0).status));
    }

    void testZeroContainingInputUsesCanonicalCobsBlock()
    {
        const uint8_t source[] = { 0x11u, 0, 0x22u, 0x33u, 0 };
        const uint8_t expected[] = { 0x02u, 0x11u, 0x03u, 0x22u, 0x33u, 0x01u };
        uint8_t encoded[sizeof(expected)] = {};
        const tinylink::codec::CobsResult result =
            tinylink::codec::cobs_encode(
                source, sizeof(source), encoded, sizeof(encoded));

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::Success),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(sizeof(expected), result.bytesWritten);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, encoded, sizeof(expected));

        uint8_t decoded[sizeof(source)] = {};
        const tinylink::codec::CobsResult roundTrip =
            tinylink::codec::cobs_decode(
                encoded, result.bytesWritten, decoded, sizeof(decoded));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::Success),
            static_cast<int>(roundTrip.status));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(source, decoded, sizeof(source));
    }

    void testNonzeroRunBoundariesRoundTripAt254ByteBlocks()
    {
        const size_t lengths[] = { 253, 254, 255, 507, 508, 509 };
        uint8_t source[509];
        uint8_t encoded[512];
        uint8_t decoded[509];
        memset(source, 0xA7, sizeof(source));

        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            const size_t length = lengths[i];
            size_t required = 0;
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(tinylink::codec::cobs_encoded_size(
                    length, required)));
            const tinylink::codec::CobsResult encodedResult =
                tinylink::codec::cobs_encode(
                    source, length, encoded, required);
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(encodedResult.status));
            TEST_ASSERT_EQUAL_UINT32(required, encodedResult.bytesWritten);
            TEST_ASSERT_EQUAL_UINT8(
                length < 254u
                    ? static_cast<uint8_t>(length + 1u)
                    : 0xFFu,
                encoded[0]);

            const tinylink::codec::CobsResult decodedResult =
                tinylink::codec::cobs_decode(
                    encoded, encodedResult.bytesWritten, decoded, length);
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(decodedResult.status));
            TEST_ASSERT_EQUAL_UINT32(length, decodedResult.bytesWritten);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(source, decoded, length);
        }
    }

    void testEncodeRejectsInvalidArgumentsAndShortCapacityUnchanged()
    {
        const uint8_t input = 0x42u;
        uint8_t output[] = { 0xA1u, 0xA2u, 0xA3u };
        const uint8_t original[] = { 0xA1u, 0xA2u, 0xA3u };

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_encode(
                nullptr, 1, output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_encode(
                &input, 1, nullptr, 1).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::OutputTooSmall),
            static_cast<int>(tinylink::codec::cobs_encode(
                &input, 1, output, 1).status));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original, output, sizeof(output));
    }

    void testDecodedSizeRejectsEmptyNullZeroAndTruncatedBlocks()
    {
        const uint8_t zeroCode[] = { 0 };
        const uint8_t truncated[] = { 4, 0x11u, 0x22u };
        size_t decodedSize = 777;

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_decoded_size(
                nullptr, 1, decodedSize)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_decoded_size(
                zeroCode, 0, decodedSize)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidEncoding),
            static_cast<int>(tinylink::codec::cobs_decoded_size(
                zeroCode, sizeof(zeroCode), decodedSize)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidEncoding),
            static_cast<int>(tinylink::codec::cobs_decoded_size(
                truncated, sizeof(truncated), decodedSize)));
        TEST_ASSERT_EQUAL_UINT32(777, decodedSize);
    }

    void testDecodeRejectsInvalidBlocksAndShortCapacityWithoutChangingOutput()
    {
        const uint8_t zeroCode[] = { 0 };
        const uint8_t truncated[] = { 3, 0x11u };
        const uint8_t valid[] = { 3, 0x11u, 0x22u };
        uint8_t output[] = { 0xA1u, 0xA2u, 0xA3u };
        const uint8_t original[] = { 0xA1u, 0xA2u, 0xA3u };

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_decode(
                nullptr, 1, output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidArgument),
            static_cast<int>(tinylink::codec::cobs_decode(
                valid, 0, output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidEncoding),
            static_cast<int>(tinylink::codec::cobs_decode(
                zeroCode, sizeof(zeroCode), output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::InvalidEncoding),
            static_cast<int>(tinylink::codec::cobs_decode(
                truncated, sizeof(truncated), output, sizeof(output)).status));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::codec::CobsStatus::OutputTooSmall),
            static_cast<int>(tinylink::codec::cobs_decode(
                valid, sizeof(valid), output, 1).status));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original, output, sizeof(output));
    }

    void testDeterministicBytePatternsRoundTripAcrossLengths()
    {
        uint8_t source[1024];
        uint8_t encoded[1030];
        uint8_t decoded[1024];

        for (size_t i = 0; i < sizeof(source); ++i) {
            source[i] = static_cast<uint8_t>((i * 73u + i / 7u + 19u) & 0xFFu);
        }

        for (size_t length = 0; length <= sizeof(source); ++length) {
            const tinylink::codec::CobsResult enc =
                tinylink::codec::cobs_encode(
                    length == 0 ? nullptr : source,
                    length,
                    encoded,
                    sizeof(encoded));
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(enc.status));
            size_t measured = 0;
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(tinylink::codec::cobs_decoded_size(
                    encoded, enc.bytesWritten, measured)));
            TEST_ASSERT_EQUAL_UINT32(length, measured);
            const tinylink::codec::CobsResult dec =
                tinylink::codec::cobs_decode(
                    encoded, enc.bytesWritten,
                    length == 0 ? nullptr : decoded, sizeof(decoded));
            TEST_ASSERT_EQUAL(
                static_cast<int>(tinylink::codec::CobsStatus::Success),
                static_cast<int>(dec.status));
            TEST_ASSERT_EQUAL_UINT32(length, dec.bytesWritten);
            if (length > 0) {
                TEST_ASSERT_EQUAL_UINT8_ARRAY(source, decoded, length);
            }
        }
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testEncodedSizeIncludesExactBlockOverhead);
    RUN_TEST(testEncodedSizeReportsOverflowWithoutChangingOutput);
    RUN_TEST(testEmptyInputHasCanonicalOneByteEncoding);
    RUN_TEST(testZeroContainingInputUsesCanonicalCobsBlock);
    RUN_TEST(testNonzeroRunBoundariesRoundTripAt254ByteBlocks);
    RUN_TEST(testEncodeRejectsInvalidArgumentsAndShortCapacityUnchanged);
    RUN_TEST(testDecodedSizeRejectsEmptyNullZeroAndTruncatedBlocks);
    RUN_TEST(testDecodeRejectsInvalidBlocksAndShortCapacityWithoutChangingOutput);
    RUN_TEST(testDeterministicBytePatternsRoundTripAcrossLengths);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
