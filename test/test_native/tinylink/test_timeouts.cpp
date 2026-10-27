/**
 * @file test_timeouts.cpp
 * @brief Inter-byte timeout boundaries, clock wrap, and fragmented arrivals.
 */

#include <unity.h>
#include <stdio.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/**
 * @brief TEST: Inter-byte Timeout Cleanup.
 * Verifies that the buffer self-clears after a partial transmission timeout.
 */

void test_timeout_cleanup(void) {
    uint8_t partial[] = { 0x00, 0x05, 0x01 };
    adapter.inject(partial, sizeof(partial));
    link.update();

    adapter.advanceMillis(300);
    link.update();

    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().timeout);

    TestPayload data = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Engine failed to recover after timeout");
}

/**
 * @brief TEST: Timeout Boundary Condition.
 * Verifies that a packet arriving at 240ms (just under the 250ms limit)
 * is accepted, while ensuring the timer resets correctly.
 */

void test_timeout_boundary(void) {
    TestPayload data = { 123, 4.56f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    // Grab the raw bytes from the loopback
    std::vector<uint8_t> raw = adapter.getRawBuffer();
    adapter.getRawBuffer().clear();

    // Inject only half the packet
    adapter.inject(raw.data(), raw.size() / 2);
    link.update();

    // Wait almost until the timeout
    adapter.advanceMillis(240);
    link.update();

    // Inject the second half
    adapter.inject(raw.data() + (raw.size() / 2), raw.size() - (raw.size() / 2));
    while(adapter.available() > 0) link.update();

    char diagnostic[120];
    snprintf(diagnostic, sizeof(diagnostic),
        "valid frame (input length %u) was rejected after a 240ms split",
        static_cast<unsigned>(raw.size()));
    TEST_ASSERT_TRUE_MESSAGE(link.available(), diagnostic);
}

/** @test Verifies the precise boundary of the inter-byte timeout logic */

void test_timeout_precision(void) {
    uint8_t partial = 0xFF;
    adapter.inject(&partial, 1);
    link.update();

    // Wait exactly at the edge (249ms)
    adapter.advanceMillis(249);
    link.update();
    TEST_ASSERT_TRUE_MESSAGE(link.getStats().timeout == 0,
        "partial byte 0xFF must not time out at 249ms (threshold is 250ms)");

    // Cross the edge (2ms later)
    adapter.advanceMillis(2);
    link.update();
    TEST_ASSERT_TRUE_MESSAGE(link.getStats().timeout == 1,
        "partial byte 0xFF must time out at 251ms (threshold is 250ms)");

    link.reset();
    adapter.setMillis(~0UL - 100UL);
    adapter.inject(&partial, 1);
    link.update();

    // Elapsed-time checks remain correct when millis() wraps around.
    adapter.advanceMillis(200);
    link.update();
    TEST_ASSERT_TRUE_MESSAGE(link.getStats().timeout == 0,
        "clock wrap: 200ms elapsed must remain below the 250ms timeout");

    adapter.advanceMillis(51);
    link.update();
    TEST_ASSERT_TRUE_MESSAGE(link.getStats().timeout == 1,
        "clock wrap: 251ms elapsed must cross the 250ms timeout");
}

/** @test Verifies that the state machine persists across fragmented single-byte arrivals */

void test_fragmented_arrival_persistence(void) {
    TestPayload data = { 5, 5.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    std::vector<uint8_t> raw = adapter.getRawBuffer();
    adapter.getRawBuffer().clear();

    for(uint8_t b : raw) {
        adapter.inject(&b, 1);
        adapter.advanceMillis(50); // Significant delay, but < 250ms
        link.update();
    }
    char diagnostic[128];
    snprintf(diagnostic, sizeof(diagnostic),
        "fragmented payload uptime=5 (%u bytes, 50ms/byte) was not delivered",
        static_cast<unsigned>(raw.size()));
    TEST_ASSERT_TRUE_MESSAGE(link.available(), diagnostic);
}

void register_timeout_tests(void) {
    RUN_TEST(test_timeout_cleanup);
    RUN_TEST(test_timeout_boundary);
    RUN_TEST(test_timeout_precision);
    RUN_TEST(test_fragmented_arrival_persistence);
}
