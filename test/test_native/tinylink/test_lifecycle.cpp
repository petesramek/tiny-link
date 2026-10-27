/**
 * @file test_lifecycle.cpp
 * @brief Frame delimiter handling, resynchronization, and partial-frame reset.
 */

#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/**
 * @brief TEST: Junk Resynchronization.
 * Verifies that the 0x00 delimiter allows recovery from leading junk data.
 */

void test_cobs_resync(void) {
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 };
    adapter.inject(junk, sizeof(junk));
    link.update();

    TestPayload data = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Link failed to resync after junk data");
}

/**
 * @brief TEST: Noise Filtering.
 * Verifies that double delimiters (0x00 0x00) are ignored and do not cause CRC errors.
 */

void test_cobs_double_delimiter(void) {
    uint8_t doubleZero[] = { 0x00, 0x00 };
    adapter.inject(doubleZero, sizeof(doubleZero));

    link.update();

    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
}

/**
 * @brief TEST: Stuttering Sync.
 * Verifies resilience to multiple 0x00 bytes preceding a valid frame.
 */

void test_double_delimiter_resilience(void) {
    TestPayload data = { 123, 1.23f };
    uint8_t noise[] = { 0x00, 0x00, 0x00 };
    adapter.inject(noise, sizeof(noise));

    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
}

/** @test Verifies that a new 0x00 delimiter immediately kills any pending partial frame */

void test_mid_frame_sync_reset(void) {
    uint8_t partial[] = { 0x00, 0x05, 0x01, 0x02 }; // Partial frame
    adapter.inject(partial, sizeof(partial));
    link.update();

    // Immediately send a full valid packet starting with 0x00
    TestPayload data = { 5, 5.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    while(adapter.available() > 0) link.update();

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "New sync byte failed to reset partial state");
    TEST_ASSERT_EQUAL_UINT32(5, link.peek().uptime);

    link.flush();
    uint8_t partialAgain[] = { 0x00, 0x05, 0x01, 0x02 };
    adapter.inject(partialAgain, sizeof(partialAgain));
    link.update();
    uint8_t startAgain = 0x00;
    adapter.inject(&startAgain, 1);
    link.update();

    TestPayload restarted = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), restarted);
    while(adapter.available() > 0) link.update();

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Double start failed to reset state machine");
    TEST_ASSERT_EQUAL_UINT32(1, link.peek().uptime);
}

/** @test Verifies that the engine handles a startup sequence of multiple delimiters safely */

void test_cold_boot_sync(void) {
    uint8_t initialNoise[] = { 0x00, 0x00, 0x00, 0x00 };
    adapter.inject(initialNoise, sizeof(initialNoise));
    link.update();

    // Should be silent, no errors
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

/** @test Verifies that a continuous stream of delimiters (0x00) doesn't trigger false errors or hangs */

void test_endless_delimiter_resilience(void) {
    uint8_t zeros[100]; memset(zeros, 0, 100);
    adapter.inject(zeros, 100);

    link.update();
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

/** @test Verifies that a delimiter appearing inside the expected payload kills the frame */

void test_mid_payload_delimiter_reset(void) {
    uint8_t header[] = { 0x04, 'D', 0x01, 0x08 }; // Valid looking header start
    adapter.inject(header, sizeof(header));
    link.update();

    uint8_t reset = 0x00; // Sudden reset
    adapter.inject(&reset, 1);
    link.update();

    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

void register_lifecycle_tests(void) {
    RUN_TEST(test_cobs_resync);
    RUN_TEST(test_cobs_double_delimiter);
    RUN_TEST(test_double_delimiter_resilience);
    RUN_TEST(test_mid_frame_sync_reset);
    RUN_TEST(test_cold_boot_sync);
    RUN_TEST(test_endless_delimiter_resilience);
    RUN_TEST(test_mid_payload_delimiter_reset);
}
