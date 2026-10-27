/**
 * @file test_overflow_recovery.cpp
 * @brief Recovery and rejection tests for oversized or malformed COBS input.
 */

#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/**
 * @brief TEST: Buffer Overflow Protection.
 * Verifies that the internal buffer safely resets when receiving non-zero runaway data.
 */

void test_buffer_overflow_protection(void) {
    for(int i = 0; i < 300; i++) {
        uint8_t junk = 0xFF;
        adapter.inject(&junk, 1);
    }
    link.update();

    TestPayload data = { 88, 8.8f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(),
        "No recovery after 300 consecutive 0xFF bytes followed by a valid frame");
}

/** @test Verifies that packets larger than the compiled struct size are rejected */

void test_oversized_struct_protection(void) {
    // Inject 110 bytes of raw non-delimiter bytes. No 0x00 is present, so the
    // frame is never completed; the buffer overflows before a delimiter arrives.
    uint8_t largeRaw[110];
    memset(largeRaw, 0xAA, 110);
    adapter.inject(largeRaw, 110);
    link.update();

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().overflow);
}

/** @test Verifies that COBS decoder fails safely if a 'code' points outside the physical buffer */

void test_cobs_read_overflow_safety(void) {
    // 0x0A (10) but we only provide 3 bytes
    uint8_t evilFrame[] = { 0x0A, 0x01, 0x02, 0x00 };
    adapter.inject(evilFrame, sizeof(evilFrame));
    link.update();

    // Should return dLen = 0 and NOT increment packets
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().packets);

    // Code 0x01 means "next zero is here", but we provide 0xFF instead
    uint8_t evil[] = { 0x01, 0xFF, 0x00 };
    adapter.inject(evil, sizeof(evil));
    link.update();

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().packets);
}

/** @test Verifies that the decoder treats truncated COBS frames as invalid (Read overflow) */

void test_cobs_truncation_safety(void) {
    // 0xFF means "next zero in 255", but we only give 10 bytes
    uint8_t truncated[] = { 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00 };
    adapter.inject(truncated, sizeof(truncated));
    link.update();

    TEST_ASSERT_FALSE(link.available());
    // Should result in dLen = 0
}

/** @test Verifies that a truncated COBS frame is rejected before decoding completes */

void test_truncated_cobs_rejection(void) {
    // Code says 10 bytes, but we only give 3
    uint8_t shorty[] = { 0x0A, 0x01, 0x02, 0x00 };
    adapter.inject(shorty, sizeof(shorty));
    link.update();
    TEST_ASSERT_FALSE(link.available());
}

void register_overflow_recovery_tests(void) {
    RUN_TEST(test_buffer_overflow_protection);
    RUN_TEST(test_oversized_struct_protection);
    RUN_TEST(test_cobs_read_overflow_safety);
    RUN_TEST(test_cobs_truncation_safety);
    RUN_TEST(test_truncated_cobs_rejection);
}
