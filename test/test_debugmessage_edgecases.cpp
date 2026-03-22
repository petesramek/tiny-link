/**
 * @file test_debugmessage_edgecases.cpp
 * @brief Edge-case unit tests for DebugMessage (DebugMessage.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/DebugMessage.h"

using namespace tinylink;

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

void register_debugmessage_edge_tests(void) {
    RUN_TEST(test_debugmessage_empty_string);
    RUN_TEST(test_debugmessage_exact_capacity);
    RUN_TEST(test_debugmessage_one_over_capacity);
    RUN_TEST(test_debugmessage_ts_boundary);
    RUN_TEST(test_debugmessage_capacity_constant);
    RUN_TEST(test_debugmessage_always_nul_terminated);
}
