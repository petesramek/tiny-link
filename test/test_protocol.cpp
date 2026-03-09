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

// TEST: Verify Fletcher-16 Loopback
void test_fletcher16_loopback(void) {
    TestPayload data = { 54321, 123.456f };
    link.send(TYPE_DATA, data);

    // Drain buffer
    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, data.value, link.peek().value);
}

// TEST: Verify Header Corruption (Fletcher-16 detection)
void test_header_fletcher_corruption(void) {
    // [SOH][Type][Seq][Len][H-CHK_L][H-CHK_H]
    uint8_t badHeader[] = { SOH, 'D', 0x01, 0x04, 0x00, 0x00 }; // Forced wrong CHK
    adapter.inject(badHeader, sizeof(badHeader));
    
    link.update();
    
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

// TEST: Verify Payload Corruption (Fletcher-16 detection)
void test_payload_fletcher_corruption(void) {
    TestPayload data = { 1, 1.0f };
    link.send(TYPE_DATA, data);
    
    // Manually corrupt the very last byte of the buffer (P-CHK_H)
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    buf[buf.size() - 2] ^= 0xFF; // Flip bits in the checksum
    
    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

// TEST: Multi-packet burst with Flow Control
void test_burst_with_fletcher(void) {
    TestPayload p1 = {1, 1.1f};
    TestPayload p2 = {2, 2.2f};
    
    link.send(TYPE_DATA, p1);
    link.send(TYPE_DATA, p2);
    
    // Process P1
    while(!link.available()) link.update();
    TEST_ASSERT_EQUAL_UINT32(1, link.peek().uptime);
    link.flush();
    
    // Process P2
    while(!link.available() && adapter.available() > 0) link.update();
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(2, link.peek().uptime);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fletcher16_loopback);
    RUN_TEST(test_header_fletcher_corruption);
    RUN_TEST(test_payload_fletcher_corruption);
    RUN_TEST(test_burst_with_fletcher);
    return UNITY_END();
}
