#include <cstdint>
#include "globals.h"

/** @brief Shared callback observation; reset by the Unity setUp() fixture. */
bool g_callbackTriggered = false;

/**
 * @brief Record callback invocation without retaining the received payload.
 * @param data Received payload, intentionally unused by this boolean probe.
 */
void testCallback(const TestPayload& data) {
    (void)data;
    g_callbackTriggered = true;
}

LoopbackAdapter adapter;
tinylink::TinyLink<TestPayload, LoopbackAdapter> link(adapter);
