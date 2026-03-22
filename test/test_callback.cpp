/**
 * @file test_callback.cpp
 * @brief Regression tests for callback-mode multi-frame reception.
 *
 * Verifies that registering an onReceive callback does not stall the RX
 * engine after the first packet. The engine must continue processing
 * subsequent frames without requiring the user to call flush().
 */

#include <unity.h>
#include "globals.h"

/**
 * @brief TEST: Callback-mode continuous reception.
 *
 * Feeds two valid encoded frames to a TinyLink instance with a registered
 * onReceive callback. Asserts the callback is invoked exactly twice,
 * proving that the engine does not stall after the first packet.
 *
 * This is a regression test for the bug where _hasNew was set to true even
 * in callback mode, causing update() to short-circuit on subsequent calls.
 */
void test_callback_receives_multiple_frames(void) {
    static int callCount;
    callCount = 0;

    link.onReceive([](const TestPayload&) { callCount++; });

    TestPayload p = { 42, 1.5f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, callCount,
        "Callback must fire for every frame; engine must not stall after first callback");
}

/**
 * @brief TEST: Callback-mode does not set available() flag.
 *
 * When a callback is registered, the engine must NOT set _hasNew, so that
 * available() returns false after reception (the callback already handled
 * the data; no polling is needed).
 */
void test_callback_mode_does_not_set_available(void) {
    link.onReceive([](const TestPayload&) { /* handled */ });

    TestPayload p = { 7, 0.1f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE_MESSAGE(link.available(),
        "Callback mode must not set available(); polling flag must remain clear");
}

/**
 * @brief TEST: Polling mode still sets available() flag when no callback registered.
 *
 * Confirms that removing the callback (nullptr) restores normal polling behaviour.
 */
void test_polling_mode_sets_available_without_callback(void) {
    /* No callback registered (setUp already cleared it) */

    TestPayload p = { 99, 9.9f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(),
        "Polling mode must set available() when no callback is registered");
}

void register_callback_tests(void) {
    RUN_TEST(test_callback_receives_multiple_frames);    /**< Core regression: no stall in callback mode */
    RUN_TEST(test_callback_mode_does_not_set_available); /**< Callback mode clears polling flag */
    RUN_TEST(test_polling_mode_sets_available_without_callback); /**< Polling mode unaffected */
}
