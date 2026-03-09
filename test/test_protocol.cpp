#include <unity.h>
#include <vector>     // Added missing include
#include <stdint.h>
#include "TinyLink.h"
#include "adapters/TinyTestAdapter.h"

struct TestPayload {
    uint32_t uptime;
    float value;
};

// Use the namespace for the base class
class LoopbackAdapter : public tinylink::TinyTestAdapter {
public:
    // Base class methods must be virtual in TinyTestAdapter.h for this to work
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

LoopbackAdapter adapter;
// Use explicit namespacing for the Link
tinylink::TinyLink<TestPayload, LoopbackAdapter> link(adapter);

void setUp(void) {
    link.flush();
    link.clearStats();
    adapter.setMillis(0);
}

void test_cobs_loopback(void) {
    TestPayload data = { 98765, 3.1415f };
    link.send(tinylink::TYPE_DATA, data); // Added namespace prefix

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, data.value, link.peek().value);
}

void test_cobs_resync(void) {
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 };
    adapter.inject(junk, sizeof(junk));
    link.update();

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    while(adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
}

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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cobs_loopback);
    RUN_TEST(test_cobs_resync);
    RUN_TEST(test_cobs_crc_failure);
    return UNITY_END();
}