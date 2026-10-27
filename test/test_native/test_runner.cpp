/**
 * @file test_runner.cpp
 * @brief Unity entry point, suite registration, and shared fixture hooks.
 */

#include <unity.h>
#include "support/stubs/globals.h"

// Forward declarations of the registration functions implemented in each test file.
void register_fletcher16_tests(void);
void register_cobs_tests(void);
void register_packet_tests(void);
void register_lifecycle_tests(void);
void register_application_tests(void);
void register_ack_sequence_tests(void);
void register_receive_mailbox_tests(void);
void register_timeout_tests(void);
void register_overflow_recovery_tests(void);
void register_telemetry_tests(void);
void register_callback_tests(void);
void register_logging_tests(void);
void register_sendack_tests(void);
void register_stats_tests(void);
void register_ackmessage_tests(void);
void register_logmessage_tests(void);
void register_message_type_tests(void);
void register_status_tests(void);
void register_handshake_tests(void);
void register_duplex_tests(void);
void register_write_result_tests(void);
void register_write_propagation_tests(void);
#if !defined(_WIN32)
void register_posix_adapter_tests(void);
#endif

/**
 * @brief Restore shared loopback state before each Unity test.
 *
 * Clearing RX bytes, virtual time, link state, and the callback flag prevents
 * one test's injected frames or callback result from influencing another.
 */
void setUp(void) {
    link.reset();
    adapter.setMillis(0);
    adapter.getRawBuffer().clear();
    g_callbackTriggered = false;
}

/**
 * @brief No post-test cleanup is needed: setUp() fully resets shared state.
 * @note Tests with local static duplex links reset those instances explicitly.
 */
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
    register_lifecycle_tests();
    register_application_tests();
    register_ack_sequence_tests();
    register_receive_mailbox_tests();
    register_timeout_tests();
    register_overflow_recovery_tests();
    register_telemetry_tests();
    register_callback_tests();
    register_logging_tests();
    register_sendack_tests();
    register_stats_tests();
    register_ackmessage_tests();
    register_logmessage_tests();
    register_message_type_tests();
    register_status_tests();
    register_handshake_tests();
    register_duplex_tests();
    register_write_result_tests();
    register_write_propagation_tests();
#if !defined(_WIN32)
    register_posix_adapter_tests();
#endif

    return UNITY_END();
}