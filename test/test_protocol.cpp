/**
 * @file test_protocol.cpp
 * @brief Unit tests for TinyLink v0.4.0 (COBS + Fletcher-16)
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
    adapter.setMillis(0);
}

/** @test Verifies end-to-end success: Send -> COBS Encode -> Decode -> Verify Fletcher-16 */
void test_cobs_loopback(void) {
    TestPayload data = { 98765, 3.1415f };
    link.send(tinylink::TYPE_DATA, data);

    // Process the bytes until the link reports a new packet available
    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, data.value, link.peek().value);
}

/** @test Verifies that the 0x00 delimiter allows recovery from partial/junk data */
void test_cobs_resync(void) {
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 }; // No 0x00 in this junk
    adapter.inject(junk, sizeof(junk));
    link.update(); // Should attempt to parse but find no delimiter

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Link failed to resync after junk data");
}

/** @test Verifies that a bit-flip in the Fletcher-16 checksum causes a rejection */
void test_cobs_crc_failure(void) {
    TestPayload data = { 10, 2.0f };
    link.send(tinylink::TYPE_DATA, data);

    // Manipulate the raw stream: Corrupt the Fletcher-16 high-byte
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

/** @brief Entry point for PlatformIO Native Test Runner */
int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_loopback);
    RUN_TEST(test_cobs_resync);
    RUN_TEST(test_cobs_crc_failure);
    return UNITY_END();
}
