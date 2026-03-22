#pragma once

#include "TinyLink.h"
#include "adapters/TinyTestAdapter.h"

/** @brief Sample payload to test struct alignment and float precision */
struct TestPayload {
    uint32_t uptime;
    float value;
} __attribute__((packed));

/** @brief Global flag for callback verification */
bool g_callbackTriggered;

/** @brief Global callback handler */
void testCallback(const TestPayload& data);

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
extern tinylink::TinyLink<TestPayload, LoopbackAdapter> link;
extern bool g_callbackTriggered;