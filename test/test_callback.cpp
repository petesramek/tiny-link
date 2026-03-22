/**
 * @file test_callback.cpp
 * @brief Unit tests verifying that callback mode continues processing
 *        subsequent frames after the first one is received.
 *
 * Regression test for the bug where registering an onReceive callback caused
 * the RX engine to stop processing further incoming frames (because _hasNew
 * was set even in callback mode, triggering the early-return guard in update()).
 */

#include <unity.h>
#include <stdint.h>
#include "globals.h"

// ---------------------------------------------------------------------------
// Callback-specific payload (distinct from the global TestPayload)
// ---------------------------------------------------------------------------

/** @brief Small packed struct used exclusively in this test file. */
struct CallbackData {
    uint32_t a;
    uint16_t b;
} __attribute__((packed));

// ---------------------------------------------------------------------------
// Adapter and link local to this test file
// ---------------------------------------------------------------------------

/** @brief Loopback adapter that pipes TX directly into the RX buffer. */
class CallbackLoopbackAdapter : public tinylink::TinyTestAdapter {
public:
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

static CallbackLoopbackAdapter cbAdapter;
static tinylink::TinyLink<CallbackData, CallbackLoopbackAdapter> cbLink(cbAdapter);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/**
 * @brief TEST: Callback fires for every received frame (multi-frame burst).
 *
 * Sends two frames via the loopback adapter and verifies that the onReceive
 * callback is invoked exactly twice.  Before the fix, the second callback
 * was never triggered because _hasNew remained set after the first frame,
 * causing update() to return immediately on subsequent calls.
 */
void test_callback_fires_for_each_frame(void) {
    static int callbackCount;
    callbackCount = 0;

    cbLink.flush();
    cbLink.clearStats();
    cbLink.onReceive([](const CallbackData&) { callbackCount++; });

    CallbackData payload = { 0xDEADBEEF, 0x1234 };
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);

    while (cbAdapter.available() > 0) {
        cbLink.update();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, callbackCount,
        "onReceive callback must fire exactly once per received frame");
}

/**
 * @brief TEST: Polling mode still works when no callback is registered.
 *
 * Verifies that polling mode (available()/peek()/flush()) is unaffected by
 * the callback-mode fix.
 */
void test_polling_mode_unaffected(void) {
    cbLink.flush();
    cbLink.clearStats();
    cbLink.onReceive(nullptr);

    CallbackData payload = { 42, 7 };
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);

    while (cbAdapter.available() > 0 && !cbLink.available()) {
        cbLink.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(cbLink.available(),
        "available() must return true after a frame is received in polling mode");
    TEST_ASSERT_EQUAL_UINT32(42, cbLink.peek().a);
    TEST_ASSERT_EQUAL_UINT16(7, cbLink.peek().b);
    cbLink.flush();
    TEST_ASSERT_FALSE_MESSAGE(cbLink.available(),
        "available() must return false after flush()");
}

/**
 * @brief TEST: Metrics are correct in callback mode.
 *
 * Verifies that packets counter increments correctly even when a callback
 * is registered (i.e., telemetry is not affected by the mode).
 */
void test_callback_telemetry_increments(void) {
    static int callbackCount;
    callbackCount = 0;

    cbLink.flush();
    cbLink.clearStats();
    cbLink.onReceive([](const CallbackData&) { callbackCount++; });

    CallbackData payload = { 1, 2 };
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);
    cbLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), payload);

    while (cbAdapter.available() > 0) {
        cbLink.update();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, callbackCount,
        "Callback should have been invoked for each of the 3 frames");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, cbLink.getStats().packets,
        "packets telemetry counter must equal the number of successfully received frames");
}

// ---------------------------------------------------------------------------
// Test registration
// ---------------------------------------------------------------------------

void register_callback_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_callback_fires_for_each_frame);
    RUN_TEST(test_polling_mode_unaffected);
    RUN_TEST(test_callback_telemetry_increments);
    UNITY_END();
}