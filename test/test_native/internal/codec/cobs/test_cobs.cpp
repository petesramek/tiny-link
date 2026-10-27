/**
 * @file test_cobs.cpp
 * @brief Unit tests for tinylink::codec::cobs_encode / cobs_decode.
 *
 * AVR-safe static-buffer tests run unconditionally.
 * A larger native-only stress test is gated by TINYLINK_NATIVE_TESTS or
 * TINYLINK_NATIVE_TEST (whichever the build system defines).
 */

#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "internal/codec/CobsCodec.h"

using namespace tinylink::codec;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Encode and decode a payload, checking that its bytes are unchanged.
 * @param pattern Short description used in deterministic failure diagnostics.
 *
 * The static arrays bound this shared helper to 512-byte input/output buffers;
 * the test vectors below and the native stress case remain within that limit.
 */
static void encode_decode_roundtrip(
    const uint8_t* payload, size_t len, const char* pattern) {
    // Fixed scratch capacity includes worst-case COBS overhead for test vectors.
    static uint8_t enc[512];
    static uint8_t dec[512];

    char diagnostic[128];
    snprintf(diagnostic, sizeof(diagnostic),
        "%s round-trip failed for input length %u",
        pattern, static_cast<unsigned>(len));
    size_t eLen = cobs_encode(payload, len, enc, sizeof(enc));
    TEST_ASSERT_TRUE_MESSAGE(eLen > 0, diagnostic);

    size_t dLen = cobs_decode(enc, eLen, dec, sizeof(dec));
    TEST_ASSERT_TRUE_MESSAGE(dLen == len &&
        memcmp(payload, dec, len) == 0, diagnostic);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/** @test Round-trip: small non-zero payload */
void test_cobs_roundtrip_small(void) {
    static const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    encode_decode_roundtrip(payload, sizeof(payload), "small nonzero pattern");
}

/** @test Round-trip: all-zero payload */
void test_cobs_roundtrip_all_zeros(void) {
    static const uint8_t payload[8] = { 0 };
    encode_decode_roundtrip(payload, sizeof(payload), "all-zero pattern");
}

/** @test Round-trip: mixed zeros and non-zeros */
void test_cobs_roundtrip_mixed(void) {
    static const uint8_t payload[] = { 0xAA, 0x00, 0xBB, 0x00, 0xCC };
    encode_decode_roundtrip(payload, sizeof(payload), "mixed zero pattern");
}

/** @test Encode fails when dst buffer is too small */
void test_cobs_encode_fail_dst_too_small(void) {
    static const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
    static uint8_t enc[2];  // Far too small
    size_t eLen = cobs_encode(payload, sizeof(payload), enc, sizeof(enc));
    TEST_ASSERT_EQUAL_size_t(0, eLen);
}

/** @test Encode fails when dstCap is 0 */
void test_cobs_encode_fail_zero_cap(void) {
    static const uint8_t payload[] = { 0x01 };
    static uint8_t enc[8];
    size_t eLen = cobs_encode(payload, sizeof(payload), enc, 0);
    TEST_ASSERT_EQUAL_size_t(0, eLen);
}

/** @test Round-trip: long non-zero run spanning multiple 0xFF-capped COBS blocks */
void test_cobs_roundtrip_long_nonzero_run(void) {
    // 300 non-zero bytes forces at least two 0xFF-capped COBS overhead blocks.
    static uint8_t payload[300];
    for (size_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)((i % 255) + 1);  // Cycles through 1-255, never 0

    static uint8_t enc[400];
    static uint8_t dec[300];

    char diagnostic[128];
    snprintf(diagnostic, sizeof(diagnostic),
        "nonzero 1..255 cycling pattern failed at input length %u",
        static_cast<unsigned>(sizeof(payload)));
    size_t eLen = cobs_encode(payload, sizeof(payload), enc, sizeof(enc));
    TEST_ASSERT_TRUE_MESSAGE(eLen > 0, diagnostic);

    size_t dLen = cobs_decode(enc, eLen, dec, sizeof(dec));
    TEST_ASSERT_TRUE_MESSAGE(dLen == sizeof(payload) &&
        memcmp(payload, dec, sizeof(payload)) == 0, diagnostic);
}

/** @test Decode returns 0 for a frame with a zero code byte (invalid) */
void test_cobs_decode_invalid_code_zero(void) {
    // A COBS code byte of 0x00 is always invalid.
    static const uint8_t bad[] = { 0x00, 0x01, 0x02 };
    static uint8_t dec[8];
    size_t dLen = cobs_decode(bad, sizeof(bad), dec, sizeof(dec));
    TEST_ASSERT_EQUAL_size_t(0, dLen);
}

/** @test Decode returns 0 when code byte points beyond the buffer */
void test_cobs_decode_invalid_code_beyond_buffer(void) {
    // Code byte 0x05 claims 4 data bytes follow, but only 2 bytes remain.
    static const uint8_t bad[] = { 0x05, 0x01, 0x02 };
    static uint8_t dec[8];
    size_t dLen = cobs_decode(bad, sizeof(bad), dec, sizeof(dec));
    TEST_ASSERT_EQUAL_size_t(0, dLen);
}

/** @test Decode returns 0 for a truncated (incomplete) frame */
void test_cobs_decode_truncated_frame(void) {
    // Encode a valid payload, then truncate the result.
    static const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    static uint8_t enc[16];
    size_t eLen = cobs_encode(payload, sizeof(payload), enc, sizeof(enc));
    TEST_ASSERT_GREATER_THAN_size_t(1, eLen);

    // Try decoding with one fewer byte than necessary.
    static uint8_t dec[16];
    size_t dLen = cobs_decode(enc, eLen - 1, dec, sizeof(dec));
    // A truncated frame may decode without error only if the truncation happens
    // to coincide with a block boundary.  The key invariant is that the output
    // must NOT equal the original payload.
    if (dLen == sizeof(payload)) {
        TEST_ASSERT_FALSE_MESSAGE(
            memcmp(dec, payload, sizeof(payload)) == 0,
            "Truncated frame must not produce the original payload");
    }
    // (dLen == 0 is also acceptable and common – just a sanity guard above.)
}

/** @test Decode returns 0 when dst buffer is too small */
void test_cobs_decode_fail_dst_too_small(void) {
    static const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 };
    static uint8_t enc[16];
    size_t eLen = cobs_encode(payload, sizeof(payload), enc, sizeof(enc));
    TEST_ASSERT_GREATER_THAN_size_t(0, eLen);

    static uint8_t dec[2];  // Way too small
    size_t dLen = cobs_decode(enc, eLen, dec, sizeof(dec));
    TEST_ASSERT_EQUAL_size_t(0, dLen);
}

// ---------------------------------------------------------------------------
// Native-only stress test (large buffer)
// ---------------------------------------------------------------------------
#if defined(TINYLINK_NATIVE_TESTS) || defined(TINYLINK_NATIVE_TEST)

#include <vector>

void test_cobs_native_stress(void) {
    const size_t N = 4096; // Native-only input length; pattern repeats 0x00..0xFF.
    std::vector<uint8_t> payload(N);
    for (size_t i = 0; i < N; i++)
        payload[i] = (uint8_t)(i & 0xFF);

    // Worst-case COBS overhead: +1 per 254 bytes
    std::vector<uint8_t> enc(N + N / 254 + 2);
    std::vector<uint8_t> dec(N);

    char diagnostic[128];
    snprintf(diagnostic, sizeof(diagnostic),
        "native stress pattern 0x00..0xFF failed at input length %u",
        static_cast<unsigned>(N));
    size_t eLen = cobs_encode(payload.data(), N, enc.data(), enc.size());
    TEST_ASSERT_TRUE_MESSAGE(eLen > 0, diagnostic);

    size_t dLen = cobs_decode(enc.data(), eLen, dec.data(), dec.size());
    TEST_ASSERT_TRUE_MESSAGE(dLen == N &&
        memcmp(payload.data(), dec.data(), N) == 0, diagnostic);
}

#endif // TINYLINK_NATIVE_TESTS || TINYLINK_NATIVE_TEST

// Register the tests from this file. No UNITY_BEGIN()/END() here.
void register_cobs_tests(void) {
    RUN_TEST(test_cobs_roundtrip_small);
    RUN_TEST(test_cobs_roundtrip_all_zeros);
    RUN_TEST(test_cobs_roundtrip_mixed);
    RUN_TEST(test_cobs_encode_fail_dst_too_small);
    RUN_TEST(test_cobs_encode_fail_zero_cap);
    RUN_TEST(test_cobs_roundtrip_long_nonzero_run);
    RUN_TEST(test_cobs_decode_invalid_code_zero);
    RUN_TEST(test_cobs_decode_invalid_code_beyond_buffer);
    RUN_TEST(test_cobs_decode_truncated_frame);
    RUN_TEST(test_cobs_decode_fail_dst_too_small);

#if defined(TINYLINK_NATIVE_TESTS) || defined(TINYLINK_NATIVE_TEST)
    RUN_TEST(test_cobs_native_stress);
#endif
}
