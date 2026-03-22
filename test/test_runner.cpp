#include <unity.h>

// Forward declarations of the registration functions implemented in each test file.
void register_fletcher16_tests(void);
void register_cobs_tests(void);
void register_packet_tests(void);
void register_protocol_tests(void);

/** @brief Reset the state machine and virtual clock before every test case */
static void setUp(void) {
    link.flush();
    link.clearStats();
    link.onReceive(nullptr);
    adapter.setMillis(0);
    adapter.getRawBuffer().clear();
    g_callbackTriggered = false;
}

/** @brief Reset state after every test case */
static void tearDown(void) {
    // No specific teardown required for this suite
}

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