#include <cstdint>
#include "globals.h"

/** @brief Global flag for callback verification */
bool g_callbackTriggered = false;

/** @brief Global callback handler */
void testCallback(const TestPayload& data) { 
    g_callbackTriggered = true; 
}

LoopbackAdapter adapter;
tinylink::TinyLink<TestPayload, LoopbackAdapter> link(adapter);
