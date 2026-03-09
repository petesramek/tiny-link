#include <unity.h>
#include "TinyLink.h"
#include "./adapters/TinyTestAdapter.h"

struct TestPayload {
    uint32_t uptime;
    float value;
};

// A modified version of your TestAdapter for Loopback
class LoopbackAdapter : public TinyTestAdapter {
public:
    // Override write to automatically "inject" data back into the read buffer
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

LoopbackAdapter adapter;
TinyLink<TestPayload, LoopbackAdapter> link(adapter);

void setUp(void) {
    link.flush();
    adapter.setMillis(0);
}

void test_full_loopback_transmission(void) {
    TestPayload dataToSend = { 54321, 123.456f };
    
    // 1. Send data through the link
    // This calls adapter.write(), which our LoopbackAdapter 
    // redirects into the internal read buffer.
    link.send(TYPE_DATA, dataToSend);

    // 2. Run the state machine to process the "received" data
    // We call update multiple times to ensure the whole buffer is parsed
    while(adapter.available() > 0) {
        link.update();
    }

    // 3. Verify Integrity
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Packet should be available after loopback");
    
    const TestPayload& received = link.peek();
    TEST_ASSERT_EQUAL_UINT32(dataToSend.uptime, received.uptime);
    // Use a small epsilon for float comparison
    TEST_ASSERT_FLOAT_WITHIN(0.001f, dataToSend.value, received.value);
    
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().packets);
}

void test_corrupted_header_recovery(void) {
    // 0x01 (SOH), 'D' (Type), 0x00 (Seq), 0x05 (Len), 0xFF (WRONG CHKSUM)
    uint8_t corruptedHeader[] = { SOH, 'D', 0x00, 0x05, 0xFF }; 
    adapter.inject(corruptedHeader, sizeof(corruptedHeader));
    
    // Process all injected bytes
    while(adapter.available() > 0) {
        link.update();
    }
    
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
    TEST_ASSERT_EQUAL(TinyStatus::ERR_CRC, link.getStatus()); 
    TEST_ASSERT_EQUAL(TinyState::WAIT_SOH, link.getState()); 
}

void test_payload_checksum_failure(void) {
    // Reset stats before starting
    link.clearStats();

    uint8_t corruptedFrame[] = {
        0x01,                   // SOH
        'D', 0x01, 0x04,        // Type, Seq, Len
        (uint8_t)('D'+0x01+0x04), // H-CHK
        0x02,                   // STX
        0xDE, 0xAD, 0xBE, 0xEF, // Payload
        0x03,                   // ETX
        0xFF                    // P-CHK (WRONG)
    };

    adapter.inject(corruptedFrame, sizeof(corruptedFrame));
    
    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

void test_resynchronization_after_timeout(void) {
    link.clearStats();
    
    // 1. Inject a partial "garbage" fragment (Start of a packet that never finishes)
    uint8_t partial[] = { SOH, 'D', 0x01, sizeof(TestPayload) };
    adapter.inject(partial, sizeof(partial));
    
    link.update();
    TEST_ASSERT_EQUAL(TinyState::WAIT_H_CHK, link.getState()); // Stuck waiting for H-CHK
    
    // 2. Advance time to trigger a timeout reset
    adapter.advanceMillis(300); 
    link.update();
    
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().timeouts);
    TEST_ASSERT_EQUAL(TinyState::WAIT_SOH, link.getState()); // Should be back at start

    // 3. Immediately inject a VALID packet
    TestPayload validData = { 999, 9.9f };
    link.send(TYPE_DATA, validData); // Loopback send
    
    while(adapter.available() > 0) {
        link.update();
    }

    // 4. Verify recovery
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Should recover and receive the second packet");
    TEST_ASSERT_EQUAL_UINT32(999, link.peek().uptime);
}

void test_multi_packet_burst(void) {
    link.clearStats();
    TestPayload p1 = {1, 1.1f};
    TestPayload p2 = {2, 2.2f};
    
    link.send(TYPE_DATA, p1);
    link.send(TYPE_DATA, p2);
    
    // First update should find the first packet
    link.update();
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(1, link.peek().uptime);
    link.flush();
    
    // Second update (continuing the while loop) should find the second packet
    link.update();
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(2, link.peek().uptime);
}


int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_full_loopback_transmission);
    RUN_TEST(test_corrupted_header_recovery);
    RUN_TEST(test_payload_checksum_failure);
    RUN_TEST(test_resynchronization_after_timeout);
    RUN_TEST(test_multi_packet_burst);
    return UNITY_END();
}
