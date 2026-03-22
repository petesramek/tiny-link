#include <unity.h>

// Forward declarations of the registration functions implemented in each test file.
void register_fletcher16_tests(void);
void register_cobs_tests(void);
void register_packet_tests(void);
void register_protocol_tests(void);

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

/** @brief Sample payload to test struct alignment and float precision */
struct TestPayload {
    uint32_t uptime;
    float value;
} __attribute__((packed));

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

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // Call each file's registration function to register its tests with Unity.
    register_fletcher16_tests();
    register_cobs_tests();
    register_packet_tests();
    register_protocol_tests();

    return UNITY_END();
}