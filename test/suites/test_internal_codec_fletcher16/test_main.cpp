#include <stdint.h>

#include <unity.h>

#include "internal/codec/Fletcher16.h"
#include "support/tinylink/TestDiagnostics.h"

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}

namespace {

    uint16_t referenceFletcher16(
        const uint8_t* data,
        size_t length)
    {
        if (length == 0) {
            return 0xFFFFu;
        }

        uint16_t sum1 = 0xFFu;
        uint16_t sum2 = 0xFFu;

        for (size_t i = 0; i < length; ++i) {
            sum1 = static_cast<uint16_t>(
                (sum1 + data[i]) % 255u);
            sum2 = static_cast<uint16_t>(
                (sum2 + sum1) % 255u);
        }

        return static_cast<uint16_t>(
            (sum2 << 8) | sum1);
    }

    void testEmptyInputReturnsProtocolExceptionValue()
    {
        const uint8_t data = 0x42u;

        TEST_ASSERT_EQUAL_UINT16(
            0xFFFFu,
            tinylink::codec::fletcher16(nullptr, 0));
        TEST_ASSERT_EQUAL_UINT16(
            0xFFFFu,
            tinylink::codec::fletcher16(&data, 0));
    }

    void testKnownFletcher16Vectors()
    {
        const uint8_t abcde[] = { 'a', 'b', 'c', 'd', 'e' };
        const uint8_t digits[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

        TEST_ASSERT_EQUAL_UINT16(
            0xC8F0u,
            tinylink::codec::fletcher16(abcde, sizeof(abcde)));
        TEST_ASSERT_EQUAL_UINT16(
            0x1EDEu,
            tinylink::codec::fletcher16(digits, sizeof(digits)));
    }

    void testChecksumPlacesSum2InHighByteAndSum1InLowByte()
    {
        const uint8_t data[] = { 0x31u, 0x32u, 0x33u };

        TEST_ASSERT_EQUAL_UINT16(
            0x2B96u,
            tinylink::codec::fletcher16(data, sizeof(data)));
    }

    void testEverySingleByteValueUsesModulo255()
    {
        for (uint16_t value = 0; value <= 0xFFu; ++value) {
            const uint8_t byte = static_cast<uint8_t>(value);
            const uint16_t reduced = static_cast<uint16_t>(value % 255u);
            const uint16_t expected = static_cast<uint16_t>(
                (reduced << 8) | reduced);

            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                expected,
                tinylink::codec::fletcher16(&byte, 1),
                tinylink_test::diag("single byte value=0x%02X", (unsigned)value));
        }
    }

    void testReductionBlockBoundariesMatchIndependentReference()
    {
        const size_t lengths[] = { 19, 20, 21, 39, 40, 41 };
        uint8_t data[41];

        for (size_t i = 0; i < sizeof(data); ++i) {
            data[i] = static_cast<uint8_t>((i * 73u + 0xA5u) & 0xFFu);
        }

        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            const size_t length = lengths[i];
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                referenceFletcher16(data, length),
                tinylink::codec::fletcher16(data, length),
                tinylink_test::diag(
                    "reduction boundary input length=%u index=%u",
                    (unsigned)length, (unsigned)i));
        }
    }

    void testZeroAndMaximumBytePatternsMatchIndependentReference()
    {
        uint8_t zeroes[257] = {};
        uint8_t maximumBytes[257];

        for (size_t i = 0; i < sizeof(maximumBytes); ++i) {
            maximumBytes[i] = 0xFFu;
        }

        const size_t lengths[] = { 1, 20, 21, 255, 256, 257 };
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
            const size_t length = lengths[i];

            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                referenceFletcher16(zeroes, length),
                tinylink::codec::fletcher16(zeroes, length),
                tinylink_test::diag("all-zero input length=%u", (unsigned)length));
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                referenceFletcher16(maximumBytes, length),
                tinylink::codec::fletcher16(maximumBytes, length),
                tinylink_test::diag("all-0xFF input length=%u", (unsigned)length));
        }
    }

    void testDeterministicInputsMatchIndependentReferenceAtEveryLength()
    {
        uint8_t data[1024];
        uint32_t state = 0xC001D00Du;

        for (size_t i = 0; i < sizeof(data); ++i) {
            state = state * 1664525u + 1013904223u;
            data[i] = static_cast<uint8_t>(state >> 24);
        }

        for (size_t length = 0; length <= sizeof(data); ++length) {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(
                referenceFletcher16(data, length),
                tinylink::codec::fletcher16(data, length),
                tinylink_test::diag(
                    "LCG pattern (seed 0xC001D00D) input length=%u of %u",
                    (unsigned)length, (unsigned)sizeof(data)));
                            }
                        }

                    } // namespace
int main()
{
    UNITY_BEGIN();
    RUN_TEST(testEmptyInputReturnsProtocolExceptionValue);
    RUN_TEST(testKnownFletcher16Vectors);
    RUN_TEST(testChecksumPlacesSum2InHighByteAndSum1InLowByte);
    RUN_TEST(testEverySingleByteValueUsesModulo255);
    RUN_TEST(testReductionBlockBoundariesMatchIndependentReference);
    RUN_TEST(testZeroAndMaximumBytePatternsMatchIndependentReference);
    RUN_TEST(testDeterministicInputsMatchIndependentReferenceAtEveryLength);
    return UNITY_END();
}
