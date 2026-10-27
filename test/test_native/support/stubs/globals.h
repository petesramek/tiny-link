#pragma once

#include "TinyLink.h"
#include "TinyTestAdapter.h"

/** @brief Sample payload to test struct alignment and float precision */
struct TestPayload {
    uint32_t uptime;
    float value;
} __attribute__((packed));

/** 
 * @class LoopbackAdapter
 * @brief Simulates a physical wire by piping TX directly into the RX buffer.
 */
class LoopbackAdapter : public tinylink::TinyTestAdapter {
public:
    tinylink::TinyWriteResult write(uint8_t c) override { return write(&c, 1); }
    tinylink::TinyWriteResult write(const uint8_t* b, size_t l) override {
        if (l == 0) return tinylink::tiny_write_result(0, 0, true);
        if (!b) return tinylink::tiny_write_result(l, 0, false);
        inject(b, l);
        return tinylink::tiny_write_result(l, l, true);
    }
};

/** @brief Loopback adapter instance */
extern LoopbackAdapter adapter;
/** @brief TinyLink instance using the LoopbackAdapter */
extern tinylink::TinyLink<TestPayload, LoopbackAdapter> link;
/** @brief Global flag for callback verification */
extern bool g_callbackTriggered;
/** @brief Callback function for testing */
extern void testCallback(const TestPayload& data);