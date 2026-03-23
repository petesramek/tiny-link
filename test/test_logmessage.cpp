/**
 * @file test_logmessage.cpp
 * @brief Unit tests for LogMessage (LogMessage.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/internal/LogMessage.h"

using namespace tinylink;

/** @test sizeof(LogMessage) == 32. */
void test_logmessage_size(void) {
    TEST_ASSERT_EQUAL_size_t(32, sizeof(LogMessage));
}

/** @test Fields are stored and retrieved correctly. */
void test_logmessage_fields(void) {
    LogMessage msg;
    msg.ts    = 123456;
    msg.level = 2;
    msg.code  = 0xABCD;
    logmessage_set_text(msg, "hello");

    TEST_ASSERT_EQUAL_UINT32(123456, msg.ts);
    TEST_ASSERT_EQUAL_UINT8(2, msg.level);
    TEST_ASSERT_EQUAL_UINT16(0xABCD, msg.code);
    TEST_ASSERT_EQUAL_STRING("hello", msg.text);
}

/** @test logmessage_set_text copies up to LOG_TEXT_CAPACITY-1 chars. */
void test_logmessage_set_text_normal(void) {
    LogMessage msg;
    logmessage_set_text(msg, "test string");
    TEST_ASSERT_EQUAL_STRING("test string", msg.text);
}

/** @test logmessage_set_text truncates strings longer than capacity. */
void test_logmessage_set_text_truncate(void) {
    LogMessage msg;
    msg.code = 0xABCD; // pre-set to verify it is unaffected by set_text
    // 30 chars — longer than LOG_TEXT_CAPACITY (25); usable capacity is 24 printable chars
    logmessage_set_text(msg, "abcdefghijklmnopqrstuvwxyz1234");
    // Should be truncated to LOG_TEXT_CAPACITY - 1 = 24 chars
    TEST_ASSERT_EQUAL_size_t(LOG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[LOG_TEXT_CAPACITY - 1]);
    // code field must be unaffected
    TEST_ASSERT_EQUAL_UINT16(0xABCD, msg.code);
}

/** @test logmessage_set_text handles NULL source gracefully. */
void test_logmessage_set_text_null(void) {
    LogMessage msg;
    msg.text[0] = 'X';  // pre-fill
    logmessage_set_text(msg, NULL);
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[0]);
}

/** @test Round-trip via memcpy preserves all fields. */
void test_logmessage_memcpy_roundtrip(void) {
    LogMessage original;
    original.ts    = 9999;
    original.level = 1;
    original.code  = 0x1234;
    logmessage_set_text(original, "ping");

    uint8_t buf[32];
    memcpy(buf, &original, sizeof(original));

    LogMessage restored;
    memcpy(&restored, buf, sizeof(restored));

    TEST_ASSERT_EQUAL_UINT32(9999, restored.ts);
    TEST_ASSERT_EQUAL_UINT8(1, restored.level);
    TEST_ASSERT_EQUAL_UINT16(0x1234, restored.code);
    TEST_ASSERT_EQUAL_STRING("ping", restored.text);
}


/** @test logmessage_set_text with an empty string stores only NUL. */
void test_logmessage_empty_string(void) {
    LogMessage msg;
    msg.text[0] = 'Z';  // pre-fill to ensure overwrite
    logmessage_set_text(msg, "");
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[0]);
}

/** @test A string of exactly LOG_TEXT_CAPACITY-1 chars fits without truncation. */
void test_logmessage_exact_capacity(void) {
    LogMessage msg;
    // Build a string of exactly LOG_TEXT_CAPACITY-1 chars
    char src[LOG_TEXT_CAPACITY];
    memset(src, 'a', LOG_TEXT_CAPACITY - 1);
    src[LOG_TEXT_CAPACITY - 1] = '\0';

    logmessage_set_text(msg, src);
    TEST_ASSERT_EQUAL_size_t(LOG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_MEMORY(src, msg.text, LOG_TEXT_CAPACITY - 1);
}

/** @test A string of exactly LOG_TEXT_CAPACITY chars is truncated by one. */
void test_logmessage_one_over_capacity(void) {
    LogMessage msg;
    // Build a string of exactly LOG_TEXT_CAPACITY chars (needs truncation)
    char src[LOG_TEXT_CAPACITY + 1];
    memset(src, 'b', LOG_TEXT_CAPACITY);
    src[LOG_TEXT_CAPACITY] = '\0';

    logmessage_set_text(msg, src);
    TEST_ASSERT_EQUAL_size_t(LOG_TEXT_CAPACITY - 1, strlen(msg.text));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[LOG_TEXT_CAPACITY - 1]);
}

/** @test ts field survives memcpy round-trip at boundary values. */
void test_logmessage_ts_boundary(void) {
    LogMessage msg;
    msg.ts    = 0xFFFFFFFF;
    msg.level = 0xFF;
    msg.code  = 0xFFFF;
    logmessage_set_text(msg, "max");

    uint8_t buf[32];
    memcpy(buf, &msg, 32);

    LogMessage out;
    memcpy(&out, buf, 32);

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, out.ts);
    TEST_ASSERT_EQUAL_UINT8(0xFF, out.level);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.code);
    TEST_ASSERT_EQUAL_STRING("max", out.text);
}

/** @test LOG_TEXT_CAPACITY constant equals 25. */
void test_logmessage_capacity_constant(void) {
    TEST_ASSERT_EQUAL_UINT8(25, LOG_TEXT_CAPACITY);
}

/** @test code field is zero-initialised independently of other fields. */
void test_logmessage_code_zero(void) {
    LogMessage msg;
    msg.ts    = 1;
    msg.level = 0;
    msg.code  = 0;
    logmessage_set_text(msg, "ok");
    TEST_ASSERT_EQUAL_UINT16(0, msg.code);
}

/** @test code field survives memcpy round-trip. */
void test_logmessage_code_roundtrip(void) {
    LogMessage msg;
    msg.ts    = 0;
    msg.level = 0;
    msg.code  = 0xBEEF;
    logmessage_set_text(msg, "x");

    uint8_t buf[32];
    memcpy(buf, &msg, 32);

    LogMessage out;
    memcpy(&out, buf, 32);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.code);
}

/** @test text field is always NUL-terminated after set_text. */
void test_logmessage_always_nul_terminated(void) {
    LogMessage msg;
    // Fill with non-zero bytes first
    memset(msg.text, 0xFF, LOG_TEXT_CAPACITY);

    logmessage_set_text(msg, "hi");
    // text[2] must be '\0'
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)msg.text[2]);
}

void register_logmessage_tests(void) {
    RUN_TEST(test_logmessage_size);
    RUN_TEST(test_logmessage_fields);
    RUN_TEST(test_logmessage_set_text_normal);
    RUN_TEST(test_logmessage_set_text_truncate);
    RUN_TEST(test_logmessage_set_text_null);
    RUN_TEST(test_logmessage_memcpy_roundtrip);
    RUN_TEST(test_logmessage_empty_string);
    RUN_TEST(test_logmessage_exact_capacity);
    RUN_TEST(test_logmessage_one_over_capacity);
    RUN_TEST(test_logmessage_ts_boundary);
    RUN_TEST(test_logmessage_capacity_constant);
    RUN_TEST(test_logmessage_always_nul_terminated);
    RUN_TEST(test_logmessage_code_zero);
    RUN_TEST(test_logmessage_code_roundtrip);
}
