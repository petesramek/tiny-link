/**
 * @file test_stats.cpp
 * @brief Unit tests for TinyStats.
 */

#include <unity.h>
#include "protocol/Stats.h"

using namespace tinylink;

/** @test Default-constructed TinyStats has all counters zero. */
void test_stats_default_zero(void) {
    TinyStats s;
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
    TEST_ASSERT_EQUAL_UINT16(0, s.crcErrs);
    TEST_ASSERT_EQUAL_UINT16(0, s.timeouts);
}

/** @test Counters can be incremented and read back. */
void test_stats_increment(void) {
    TinyStats s;
    s.packets  = 100;
    s.crcErrs  = 5;
    s.timeouts = 3;
    TEST_ASSERT_EQUAL_UINT32(100, s.packets);
    TEST_ASSERT_EQUAL_UINT16(5,   s.crcErrs);
    TEST_ASSERT_EQUAL_UINT16(3,   s.timeouts);
}

/** @test clear() resets all counters to zero. */
void test_stats_clear(void) {
    TinyStats s;
    s.packets  = 42;
    s.crcErrs  = 7;
    s.timeouts = 2;
    s.clear();
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
    TEST_ASSERT_EQUAL_UINT16(0, s.crcErrs);
    TEST_ASSERT_EQUAL_UINT16(0, s.timeouts);
}

/** @test Successive clear() calls are idempotent. */
void test_stats_double_clear(void) {
    TinyStats s;
    s.packets = 1;
    s.clear();
    s.clear();
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
}

void register_stats_tests(void) {
    RUN_TEST(test_stats_default_zero);
    RUN_TEST(test_stats_increment);
    RUN_TEST(test_stats_clear);
    RUN_TEST(test_stats_double_clear);
}
