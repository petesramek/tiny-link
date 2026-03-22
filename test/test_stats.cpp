/**
 * @file test_stats.cpp
 * @brief Unit tests for TinyStats.
 */

#include <unity.h>
#include "protocol/Stats.h"
#include "protocol/Status.h"

using namespace tinylink;

/** @test Default-constructed TinyStats has all counters zero. */
void test_stats_default_zero(void) {
    TinyStats s;
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
    TEST_ASSERT_EQUAL_UINT32(0, s.crc);
    TEST_ASSERT_EQUAL_UINT32(0, s.timeout);
    TEST_ASSERT_EQUAL_UINT32(0, s.overflow);
    TEST_ASSERT_EQUAL_UINT32(0, s.busy);
    TEST_ASSERT_EQUAL_UINT32(0, s.processing);
    TEST_ASSERT_EQUAL_UINT32(0, s.unknown);
}

/** @test increment() updates the correct field for each TinyStatus. */
void test_stats_increment(void) {
    TinyStats s;
    s.increment(TinyStatus::STATUS_OK);
    TEST_ASSERT_EQUAL_UINT32(1, s.packets);

    s.increment(TinyStatus::ERR_CRC);
    TEST_ASSERT_EQUAL_UINT32(1, s.crc);

    s.increment(TinyStatus::ERR_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT32(1, s.timeout);

    s.increment(TinyStatus::ERR_OVERFLOW);
    TEST_ASSERT_EQUAL_UINT32(1, s.overflow);

    s.increment(TinyStatus::ERR_BUSY);
    TEST_ASSERT_EQUAL_UINT32(1, s.busy);

    s.increment(TinyStatus::ERR_PROCESSING);
    TEST_ASSERT_EQUAL_UINT32(1, s.processing);

    s.increment(TinyStatus::ERR_UNKNOWN);
    TEST_ASSERT_EQUAL_UINT32(1, s.unknown);
}

/** @test countFor() returns the same value as the corresponding field. */
void test_stats_count_for(void) {
    TinyStats s;
    s.increment(TinyStatus::STATUS_OK);
    s.increment(TinyStatus::STATUS_OK);
    TEST_ASSERT_EQUAL_UINT32(2, s.countFor(TinyStatus::STATUS_OK));
    TEST_ASSERT_EQUAL_UINT32(2, s.packets);

    s.increment(TinyStatus::ERR_CRC);
    TEST_ASSERT_EQUAL_UINT32(1, s.countFor(TinyStatus::ERR_CRC));
}

/** @test clear() resets all counters to zero. */
void test_stats_clear(void) {
    TinyStats s;
    s.increment(TinyStatus::STATUS_OK);
    s.increment(TinyStatus::ERR_CRC);
    s.increment(TinyStatus::ERR_TIMEOUT);
    s.increment(TinyStatus::ERR_OVERFLOW);
    s.increment(TinyStatus::ERR_BUSY);
    s.increment(TinyStatus::ERR_PROCESSING);
    s.increment(TinyStatus::ERR_UNKNOWN);
    s.clear();
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
    TEST_ASSERT_EQUAL_UINT32(0, s.crc);
    TEST_ASSERT_EQUAL_UINT32(0, s.timeout);
    TEST_ASSERT_EQUAL_UINT32(0, s.overflow);
    TEST_ASSERT_EQUAL_UINT32(0, s.busy);
    TEST_ASSERT_EQUAL_UINT32(0, s.processing);
    TEST_ASSERT_EQUAL_UINT32(0, s.unknown);
}

/** @test Successive clear() calls are idempotent. */
void test_stats_double_clear(void) {
    TinyStats s;
    s.increment(TinyStatus::STATUS_OK);
    s.clear();
    s.clear();
    TEST_ASSERT_EQUAL_UINT32(0, s.packets);
}

/** @test merge() adds counters from another TinyStats instance. */
void test_stats_merge(void) {
    TinyStats a;
    a.increment(TinyStatus::STATUS_OK);
    a.increment(TinyStatus::ERR_CRC);

    TinyStats b;
    b.increment(TinyStatus::STATUS_OK);
    b.increment(TinyStatus::STATUS_OK);
    b.increment(TinyStatus::ERR_TIMEOUT);

    a.merge(b);
    TEST_ASSERT_EQUAL_UINT32(3, a.packets);
    TEST_ASSERT_EQUAL_UINT32(1, a.crc);
    TEST_ASSERT_EQUAL_UINT32(1, a.timeout);
}

void register_stats_tests(void) {
    RUN_TEST(test_stats_default_zero);
    RUN_TEST(test_stats_increment);
    RUN_TEST(test_stats_count_for);
    RUN_TEST(test_stats_clear);
    RUN_TEST(test_stats_double_clear);
    RUN_TEST(test_stats_merge);
}
