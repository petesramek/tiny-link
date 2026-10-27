/**
 * @file test_receive_mailbox.cpp
 * @brief Receive mailbox, callback, checksum, and malformed-frame behavior.
 */

#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/** One-byte payload used as the lower side of a frame-size mismatch. */
struct Small { uint8_t a; } __attribute__((packed));

/** Eight-byte payload used as the upper side of a frame-size mismatch. */
struct Large { uint32_t a; uint32_t b; } __attribute__((packed));

/**
 * @brief TEST: Integrity Check.
 * Verifies that a bit-flip in the Fletcher-16 checksum causes rejection.
 */

void test_cobs_crc_failure(void) {
    TestPayload data = { 10, 2.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 3) {
        buf[buf.size() - 2] ^= 0xFF;
    }

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/**
 * @brief TEST: Asynchronous Callback.
 * Verifies that the onDataReceived callback fires correctly upon valid packet arrival.
 */

void test_async_callback(void) {
    link.onDataReceived(testCallback);
    TestPayload data = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(g_callbackTriggered, "Callback was not triggered");
}

/**
 * @brief TEST: Multi-packet Burst.
 * Verifies that two packets in the same buffer both trigger callbacks.
 */

void test_callback_burst(void) {
    static int triggerCount;
    triggerCount = 0;
    link.onDataReceived([](const TestPayload&) { triggerCount++; });

    TestPayload p = { 1, 1.1f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while(adapter.available() > 0) {
        link.update();
        if(link.available()) link.flush();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, triggerCount,
        "Expected callbacks for both uptime=1, value=1.1 frames in the burst");
}

/**
 * @brief TEST: Phantom Frame Rejection.
 * Verifies that frames shorter than the protocol minimum are silently ignored.
 */

void test_short_frame_rejection(void) {
    uint8_t shorty[] = { 0x00, 0x02, 0x00 };
    adapter.inject(shorty, sizeof(shorty));
    link.update();

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
}

/**
 * @brief TEST: Buffer Isolation.
 * Verifies that incoming unverified data does not corrupt current valid data.
 */

void test_buffer_isolation(void) {
    TestPayload p1 = { 111, 1.1f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();

    // Inject a partial/corrupt packet
    uint8_t partial[] = { 0x00, 0x05, 0x01 };
    adapter.inject(partial, sizeof(partial));
    link.update();

    // Valid data must remain isolated
    TEST_ASSERT_EQUAL_UINT32(111, link.peek().uptime);
}

/**
 * @brief TEST: Zero-Length Header Rejection.
 * Verifies that a valid COBS frame with a payload length (5) that
 * does not match the expected PLAIN_SIZE (13) is caught as a CRC error.
 */

void test_zero_length_header_rejection(void) {
    // A perfectly valid COBS frame of 5 bytes (No 0x00 bytes inside the data)
    // [0x06] (Code: Next zero in 6 bytes) [0x01, 0x02, 0x03, 0x04, 0x05]
    uint8_t cobs_frame[] = { 0x06, 0x01, 0x02, 0x03, 0x04, 0x05 };
    uint8_t delimiter = 0x00;

    adapter.getRawBuffer().clear();
    adapter.inject(cobs_frame, sizeof(cobs_frame));
    adapter.inject(&delimiter, 1);

    link.update();

    // dLen = 5. PLAIN_SIZE = 13.
    // 5 != 13 triggers the CrcError telemetry increment in TinyLink.h.
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that Fletcher-16 detects swapped bytes (which simple checksums miss) */

void test_swapped_byte_detection(void) {
    TestPayload data = { 0x11223344, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 10) {
        // Swap two adjacent data bytes in the raw buffer
        std::swap(buf[5], buf[6]);
    }

    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that a single bit-flip in the payload is caught by Fletcher-16 */

void test_single_bit_flip_detection(void) {
    TestPayload data = { 0x00, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Flip the LSB of the first data byte (usually at index 4 or 5 after header)
    buf[5] ^= 0x01;

    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that a single bit flipping to zero (Ghost Zero) is caught */

void test_ghost_zero_detection(void) {
    TestPayload data;
    memset(&data, 0xFF, sizeof(data));
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Flip a bit in the middle of the payload to make it 0x00
    buf[10] = 0x00;

    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE_MESSAGE(link.available(), "Ghost zero was not detected");
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that valid COBS frames with incorrect payload sizes are rejected */

void test_structural_size_mismatch(void) {
    // Deliberately differs from TestPayload by four bytes to exercise strict size rejection.
    struct NewData { uint32_t a; uint32_t b; uint32_t c; } __attribute__((packed));
    tinylink::TinyLink<NewData, LoopbackAdapter> espLink(adapter);

    NewData d = {1, 2, 3};
    espLink.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), d); // Sends 17 bytes (3+12+2)

    // TinyLink<TestPayload> expects 13 bytes (3+8+2)
    while(adapter.available() > 0) link.update();

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);

    tinylink::TinyLink<Small, LoopbackAdapter> linkSmall(adapter);
    tinylink::TinyLink<Large, LoopbackAdapter> linkLarge(adapter);
    Small small = { 0xFF };
    linkSmall.sendData('S', small);

    while(adapter.available() > 0) linkLarge.update();

    TEST_ASSERT_FALSE(linkLarge.available());
    TEST_ASSERT_EQUAL_UINT32(1, linkLarge.getStats().crc);
}

/** @test Verifies that micro-frames (noise) are silently ignored without logging errors */

void test_micro_frame_noise_suppression(void) {
    uint8_t noise[] = { 0x00, 0x01, 0x00, 0x02, 0x00 };
    adapter.inject(noise, sizeof(noise));
    link.update();

    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().packets);
}

/** @test Verifies that the engine ignores new data while 'available' flag is still true (user hasn't flushed) */

void test_reentrant_lock_safety(void) {
    TestPayload p1 = { 100, 1.0f };
    TestPayload p2 = { 200, 2.0f };

    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();

    // Packet 1 is now 'available'. Inject Packet 2.
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p2);
    link.update(); // This should be ignored because _hasNew is true

    TEST_ASSERT_EQUAL_UINT32(100, link.peek().uptime);
    link.flush(); // Now Packet 2 can be processed on next update
}

/** @test Verifies that the 16-bit checksum is packed and unpacked in a fixed endian order */

void test_endian_checksum_consistency(void) {
    TestPayload data = { 0xAABBCCDD, 1.23f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();

    // The last two bytes of the COBS frame (before the 0x00) are the Fletcher-16.
    // We swap them manually. If the engine is endian-sensitive, it must fail.
    if (buf.size() > 3) {
        std::swap(buf[buf.size()-2], buf[buf.size()-3]);
    }

    while(adapter.available() > 0) link.update();

    // If the checksum was correctly packed/unpacked, swapping bytes MUST cause a CRC error.
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);

    link.reset();
    TestPayload boundary = { 0xDEADBEEF, 3.14f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), boundary);
    std::vector<uint8_t>& boundaryBuffer = adapter.getRawBuffer();
    std::swap(boundaryBuffer[boundaryBuffer.size() - 2],
        boundaryBuffer[boundaryBuffer.size() - 3]);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that changing the callback during an active reception is safe */

void test_callback_hotswap_safety(void) {
    uint8_t start[] = { 0x00, 0x05, 0x01 };
    adapter.inject(start, sizeof(start));
    link.update();

    static bool hotswapFired;
    hotswapFired = false;
    link.onDataReceived([](const TestPayload&){ hotswapFired = true; });

    // Complete the packet
    TestPayload data = {1, 1.0f};
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_TRUE_MESSAGE(hotswapFired, "Hot-swapped callback was not invoked");
}

/** @test Verifies that the checksum for one packet doesn't affect the next calculation */

void test_checksum_independence(void) {
    TestPayload p1 = { 0x11111111, 1.0f };
    TestPayload p2 = { 0x22222222, 2.0f };

    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().packets);
    link.flush();

    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p2);
    while(adapter.available() > 0) link.update();

    TEST_ASSERT_EQUAL_UINT32(2, link.getStats().packets);
}

/** @test Verifies that the checksum protects the very end of the payload */

void test_trailing_bit_integrity(void) {
    TestPayload data = { 0, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Index 10 is typically the last byte of an 8-byte payload in a 13-byte frame
    buf[buf.size() - 3] ^= 0x01;

    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.getStats().crc);
}

/** @test Verifies that unregistering a callback mid-stream doesn't cause a null-pointer crash */

void test_callback_deregistration_safety(void) {
    link.onDataReceived(testCallback);
    TestPayload p = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    // Halfway through, kill the callback
    link.onDataReceived(nullptr);

    while(adapter.available() > 0) link.update();
    // Should NOT crash, should just finish silently
    TEST_ASSERT_TRUE(link.available());
}

/** @test Verifies that new junk data cannot overwrite un-flushed valid data */

void test_unflushed_data_protection(void) {
    TestPayload p1 = { 0xDEADC0DE, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();

    // Inject 50 bytes of 0xFF junk
    uint8_t junk[50]; memset(junk, 0xFF, 50);
    adapter.inject(junk, 50);
    link.update();

    TEST_ASSERT_EQUAL_UINT32(0xDEADC0DE, link.peek().uptime);
}

void register_receive_mailbox_tests(void) {
    RUN_TEST(test_cobs_crc_failure);
    RUN_TEST(test_async_callback);
    RUN_TEST(test_callback_burst);
    RUN_TEST(test_short_frame_rejection);
    RUN_TEST(test_buffer_isolation);
    RUN_TEST(test_zero_length_header_rejection);
    RUN_TEST(test_swapped_byte_detection);
    RUN_TEST(test_single_bit_flip_detection);
    RUN_TEST(test_ghost_zero_detection);
    RUN_TEST(test_structural_size_mismatch);
    RUN_TEST(test_micro_frame_noise_suppression);
    RUN_TEST(test_reentrant_lock_safety);
    RUN_TEST(test_endian_checksum_consistency);
    RUN_TEST(test_callback_hotswap_safety);
    RUN_TEST(test_checksum_independence);
    RUN_TEST(test_trailing_bit_integrity);
    RUN_TEST(test_callback_deregistration_safety);
    RUN_TEST(test_unflushed_data_protection);
}
