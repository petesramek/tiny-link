#include <unity.h>
#include "TinyLink.h"
#include "./adapters/TinyTestAdapter.h"

struct TestPayload {
    uint32_t uptime;
    float value;
};

class LoopbackAdapter : public TinyTestAdapter {
public:
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

LoopbackAdapter adapter;
TinyLink<TestPayload, LoopbackAdapter> link(adapter);

void setUp(void) {
    link.flush();
    link.clearStats();
    adapter.setMillis(0);
}

// 1. TEST: Basic Loopback with COBS Encoding/Decoding
void test_cobs_loopback(void) {
    TestPayload data = { 98765, 3.1415f };
    link.send(TYPE_DATA, data);

    // Update until packet is processed
    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "COBS packet should be available");
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, data.value, link.peek().value);
}

// 2. TEST: Resync after junk data (The power of COBS)
void test_cobs_resync(void) {
    // Inject random junk bytes that do NOT contain 0x00
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 };
    adapter.inject(junk, sizeof(junk));
    link.update();

    // Now inject a perfectly valid COBS packet
    TestPayload data = { 1, 1.0f };
    link.send(TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Should sync at 0x00 and ignore junk");
    TEST_ASSERT_EQUAL_UINT32(1, link.peek().uptime);
}

// 3. TEST: CRC Failure in COBS Frame
void test_cobs_crc_failure(void) {
    TestPayload data = { 10, 2.0f };
    link.send(TYPE_DATA, data);

    // Corrupt the Fletcher-16 bytes (last two bytes before the trailing 0x00)
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 3) {
        buf[buf.size() - 2] ^= 0xFF; // Flip bits in Fletcher-16
    }

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

// 4. TEST: Burst handling with Delimiters
void test_cobs_burst(void) {
    TestPayload p1 = {100, 1.0f};
    TestPayload p2 = {200, 2.0f};
    
    link.send(TYPE_DATA, p1);
    link.send(TYPE_DATA, p2);
    
    // Process P1
    while(!link.available()) link.update();
    TEST_ASSERT_EQUAL_UINT32(100, link.peek().uptime);
    link.flush();
    
    // Process P2
    while(!link.available() && adapter.available() > 0) link.update();
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(200, link.peek().uptime);
}

bool callbackFired = false;
void testCallback(const TestPayload& data) {
    callbackFired = true;
}

void test_callback_functionality(void) {
    link.onReceive(testCallback);
    callbackFired = false;

    TestPayload data = { 55, 5.5f };
    link.send(TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(callbackFired, "Callback should have been triggered");
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_loopback);
    RUN_TEST(test_cobs_resync);
    RUN_TEST(test_cobs_crc_failure);
    RUN_TEST(test_cobs_burst);
    return UNITY_END();
}
