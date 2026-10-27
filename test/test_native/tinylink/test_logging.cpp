#include <unity.h>
#include "support/stubs/globals.h"
#include "internal/protocol/LogMessage.h"

using namespace tinylink;

/**
 * @brief TEST: onLogReceived fires when an incoming log frame is received.
 */

void test_on_log_received_fires(void) {
    static bool logFired;
    static uint16_t receivedCode;
    logFired = false;
    receivedCode = 0;

    link.onLogReceived([](const LogMessage& msg) {
        logFired = true;
        receivedCode = msg.code;
    });

    link.sendLog(LogLevel::INFO, 0x1234, "hello");

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(logFired, "onLogReceived must fire for a Log frame");
    TEST_ASSERT_EQUAL_UINT16(0x1234, receivedCode);
}

/**
 * @brief TEST: Log frames do not reach the data callback.
 */

void test_log_frame_not_dispatched_as_data(void) {
    static bool dataFired;
    dataFired = false;

    link.onDataReceived([](const TestPayload&) { dataFired = true; });

    link.sendLog(LogLevel::WARN, 0, "test");

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE_MESSAGE(dataFired,
        "Log frames must not be dispatched to the data callback");
}

void register_logging_tests(void) {
    RUN_TEST(test_on_log_received_fires);
    RUN_TEST(test_log_frame_not_dispatched_as_data);
}
