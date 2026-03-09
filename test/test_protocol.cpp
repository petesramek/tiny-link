/**
 * @file test_protocol.cpp
 * @brief Unit tests for TinyLink v0.4.0 (COBS + Fletcher-16)
 * 
 * Verifies data integrity, framing robustness, and hardware-level 
 * fault tolerance (noise, timeouts, and overflows).
 */

#include <unity.h>
#include <vector>
#include <stdint.h>
#include "TinyLink.h"
#include "adapters/TinyTestAdapter.h"

/** @brief Sample payload to test struct alignment and float precision */
struct TestPayload {
    uint32_t uptime;
    float value;
};

/** @brief Global flag for callback verification */
bool g_callbackTriggered = false;

/** @brief Global callback handler */
void testCallback(const TestPayload& data) { 
    g_callbackTriggered = true; 
}

/** 
 * @class LoopbackAdapter
 * @brief Simulates a physical wire by piping TX directly into the RX buffer.
 */
class LoopbackAdapter : public tinylink::TinyTestAdapter {
public:
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

LoopbackAdapter adapter;
tinylink::TinyLink<TestPayload, LoopbackAdapter> link(adapter);

/** @brief Reset the state machine and virtual clock before every test case */
void setUp(void) {
    link.flush();
    link.clearStats();
    link.onReceive(nullptr);
    adapter.setMillis(0);
    adapter.getRawBuffer().clear();
    g_callbackTriggered = false;
}

/** @brief Reset state after every test case */
void tearDown(void) {
    // No specific teardown required for this suite
}

/** 
 * @brief TEST: End-to-end success.
 * Verifies Send -> COBS Encode -> Decode -> Verify Fletcher-16.
 */
void test_cobs_loopback(void) {
    TestPayload data = { 98765, 3.1415f };
    link.send(tinylink::TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, data.value, link.peek().value);
}

/** 
 * @brief TEST: COBS transparency.
 * Verifies that a payload consisting entirely of 0x00 is handled correctly.
 */
void test_cobs_zero_payload(void) {
    TestPayload zeros = { 0, 0.0f }; 
    link.send(tinylink::TYPE_DATA, zeros);
    
    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "COBS failed to handle all-zero payload");
}

/** 
 * @brief TEST: Junk Resynchronization.
 * Verifies that the 0x00 delimiter allows recovery from leading junk data.
 */
void test_cobs_resync(void) {
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 }; 
    adapter.inject(junk, sizeof(junk));
    link.update(); 

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Link failed to resync after junk data");
}

/** 
 * @brief TEST: Integrity Check.
 * Verifies that a bit-flip in the Fletcher-16 checksum causes rejection.
 */
void test_cobs_crc_failure(void) {
    TestPayload data = { 10, 2.0f };
    link.send(tinylink::TYPE_DATA, data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 3) {
        buf[buf.size() - 2] ^= 0xFF; 
    }

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** 
 * @brief TEST: Asynchronous Callback.
 * Verifies that the onReceive callback fires correctly upon valid packet arrival.
 */
void test_async_callback(void) {
    link.onReceive(testCallback);
    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(g_callbackTriggered, "Callback was not triggered");
}

/** 
 * @brief TEST: Noise Filtering.
 * Verifies that double delimiters (0x00 0x00) are ignored and do not cause CRC errors.
 */
void test_cobs_double_delimiter(void) {
    uint8_t doubleZero[] = { 0x00, 0x00 };
    adapter.inject(doubleZero, sizeof(doubleZero));
    
    link.update();
    
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
    TEST_ASSERT_EQUAL_UINT8(0, adapter.available());
}

/** 
 * @brief TEST: Multi-packet Burst.
 * Verifies that two packets in the same buffer both trigger callbacks.
 */
void test_callback_burst(void) {
    static int triggerCount;
    triggerCount = 0;
    link.onReceive([](const TestPayload& d) { triggerCount++; });

    TestPayload p = { 1, 1.1f };
    link.send(tinylink::TYPE_DATA, p);
    link.send(tinylink::TYPE_DATA, p);

    while(adapter.available() > 0) {
        link.update();
        if(link.available()) link.flush();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, triggerCount, "Callback should have fired exactly twice");
}

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

    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().timeouts);
    
    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Engine failed to recover after timeout");
}

/**
 * @brief TEST: Stuttering Sync.
 * Verifies resilience to multiple 0x00 bytes preceding a valid frame.
 */
void test_double_delimiter_resilience(void) {
    TestPayload data = { 123, 1.23f };
    uint8_t noise[] = { 0x00, 0x00, 0x00 };
    adapter.inject(noise, sizeof(noise));
    
    link.send(tinylink::TYPE_DATA, data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
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
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs); 
}

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
    link.send(tinylink::TYPE_DATA, data);
    
    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Overflow guard prevented recovery");
}

/**
 * @brief Entry point for the PlatformIO Native Test Runner.
 */
int main(int argc, char **argv) {
    UNITY_BEGIN();

    RUN_TEST(test_cobs_loopback);
    RUN_TEST(test_cobs_zero_payload);
    RUN_TEST(test_cobs_resync);
    RUN_TEST(test_cobs_crc_failure);
    RUN_TEST(test_async_callback);
    RUN_TEST(test_cobs_double_delimiter);
    RUN_TEST(test_callback_burst);
    RUN_TEST(test_timeout_cleanup);
    RUN_TEST(test_double_delimiter_resilience);
    RUN_TEST(test_short_frame_rejection);
    RUN_TEST(test_buffer_overflow_protection);

    return UNITY_END();
}
