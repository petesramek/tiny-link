#include <unity.h>
#include "globals.h"

// Forward declarations of the registration functions implemented in each test file.
void register_fletcher16_tests(void);
void register_cobs_tests(void);
void register_packet_tests(void);
void register_protocol_tests(void);
void register_callback_tests(void);
void register_sendack_tests(void);
void register_stats_tests(void);
void register_ackmessage_tests(void);
void register_logmessage_tests(void);
void register_message_type_tests(void);
void register_status_tests(void);
void register_handshake_tests(void);
void register_duplex_tests(void);

/** @brief Reset the state machine and virtual clock before every test case */
void setUp(void) {
    link.reset();
    adapter.setMillis(0);
    adapter.getRawBuffer().clear();
    g_callbackTriggered = false;
}

/** @brief Reset state after every test case */
void tearDown(void) {
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
    register_callback_tests();
    register_sendack_tests();
    register_stats_tests();
    register_ackmessage_tests();
    register_logmessage_tests();
    register_message_type_tests();
    register_status_tests();
    register_handshake_tests();
    register_duplex_tests();

    return UNITY_END();
}