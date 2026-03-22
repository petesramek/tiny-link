/**
 * @file test_debugmessage.cpp
 * @brief Unit tests for DebugMessage (DebugMessage.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/DebugMessage.h"

using namespace tinylink;

/** @test sizeof(DebugMessage) == 32. */
void test_debugmessage_size(void) {
    TEST_ASSERT_EQUAL_size_t(32, sizeof(DebugMessage));
}

/** @test Fields are stored and retrieved correctly. */
void test_debugmessage_fields(void) {
    DebugMessage msg;
    msg.ts    = 123456;
    msg.level = 2;
    debugmessage_set_text(msg, "hello");

    TEST_ASSERT_EQUAL_UINT32(123456, msg.ts);
    TEST_ASSERT_EQUAL_UINT8(2, msg.level);
    TEST_ASSERT_EQUAL_STRING("hello", msg.text);
}

/** @test debugmessage_set_text copies up to DEBUG_TEXT_CAPACITY-1 chars. */
void test_debugmessage_set_text_normal(void) {
    DebugMessage msg;
    debugmessage_set_text(msg, "test string");
    TEST_ASSERT_EQUAL_STRING("test string", msg.text);
}

/** @test debugmessage_set_text truncates strings longer than capacity. */
void test_debugmessage_set_text_truncate(void) {
    DebugMessage msg;
    // 30 chars — longer than DEBUG_TEXT_CAPACITY (27)
    debugmessage_set_text(msg, "abcdefghijklmnopqrstuvwxyz1234");
    // Should be truncated to DEBUG_TEXT_CAPACITY - 1 = 26 chars
    TEST_ASSERT_EQUAL_size_t(DEBUG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[DEBUG_TEXT_CAPACITY - 1]);
}

/** @test debugmessage_set_text handles NULL source gracefully. */
void test_debugmessage_set_text_null(void) {
    DebugMessage msg;
    msg.text[0] = 'X';  // pre-fill
    debugmessage_set_text(msg, NULL);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[0]);
}

/** @test Round-trip via memcpy preserves all fields. */
void test_debugmessage_memcpy_roundtrip(void) {
    DebugMessage original;
    original.ts    = 9999;
    original.level = 1;
    debugmessage_set_text(original, "ping");

    uint8_t buf[32];
    memcpy(buf, &original, sizeof(original));

    DebugMessage restored;
    memcpy(&restored, buf, sizeof(restored));

    TEST_ASSERT_EQUAL_UINT32(9999, restored.ts);
    TEST_ASSERT_EQUAL_UINT8(1, restored.level);
    TEST_ASSERT_EQUAL_STRING("ping", restored.text);
}


/** @test debugmessage_set_text with an empty string stores only NUL. */
void test_debugmessage_empty_string(void) {
    DebugMessage msg;
    msg.text[0] = 'Z';  // pre-fill to ensure overwrite
    debugmessage_set_text(msg, "");
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[0]);
}

/** @test A string of exactly DEBUG_TEXT_CAPACITY-1 chars fits without truncation. */
void test_debugmessage_exact_capacity(void) {
    DebugMessage msg;
    // Build a string of exactly DEBUG_TEXT_CAPACITY-1 chars
    char src[DEBUG_TEXT_CAPACITY];
    memset(src, 'a', DEBUG_TEXT_CAPACITY - 1);
    src[DEBUG_TEXT_CAPACITY - 1] = '\0';

    debugmessage_set_text(msg, src);
    TEST_ASSERT_EQUAL_size_t(DEBUG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_MEMORY(src, msg.text, DEBUG_TEXT_CAPACITY - 1);
}

/** @test A string of exactly DEBUG_TEXT_CAPACITY chars is truncated by one. */
void test_debugmessage_one_over_capacity(void) {
    DebugMessage msg;
    // Build a string of exactly DEBUG_TEXT_CAPACITY chars (needs truncation)
    char src[DEBUG_TEXT_CAPACITY + 1];
    memset(src, 'b', DEBUG_TEXT_CAPACITY);
    src[DEBUG_TEXT_CAPACITY] = '\0';

    debugmessage_set_text(msg, src);
    TEST_ASSERT_EQUAL_size_t(DEBUG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[DEBUG_TEXT_CAPACITY - 1]);
}

/** @test ts field survives memcpy round-trip at boundary values. */
void test_debugmessage_ts_boundary(void) {
    DebugMessage msg;
    msg.ts    = 0xFFFFFFFF;
    msg.level = 0xFF;
    debugmessage_set_text(msg, "max");

    uint8_t buf[32];
    memcpy(buf, &msg, 32);

    DebugMessage out;
    memcpy(&out, buf, 32);

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, out.ts);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out.level);
    TEST_ASSERT_EQUAL_STRING("max", out.text);
}

/** @test DEBUG_TEXT_CAPACITY constant equals 27. */
void test_debugmessage_capacity_constant(void) {
    TEST_ASSERT_EQUAL_UINT8(27, DEBUG_TEXT_CAPACITY);
}

/** @test text field is always NUL-terminated after set_text. */
void test_debugmessage_always_nul_terminated(void) {
    DebugMessage msg;
    // Fill with non-zero bytes first
    memset(msg.text, 0xFF, DEBUG_TEXT_CAPACITY);

    debugmessage_set_text(msg, "hi");
    // text[2] must be '\0'
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[2]);
}

void register_debugmessage_tests(void) {
    RUN_TEST(test_debugmessage_size);
    RUN_TEST(test_debugmessage_fields);
    RUN_TEST(test_debugmessage_set_text_normal);
    RUN_TEST(test_debugmessage_set_text_truncate);
    RUN_TEST(test_debugmessage_set_text_null);
    RUN_TEST(test_debugmessage_memcpy_roundtrip);
    RUN_TEST(test_debugmessage_empty_string);
    RUN_TEST(test_debugmessage_exact_capacity);
    RUN_TEST(test_debugmessage_one_over_capacity);
    RUN_TEST(test_debugmessage_ts_boundary);
    RUN_TEST(test_debugmessage_capacity_constant);
    RUN_TEST(test_debugmessage_always_nul_terminated);
}
