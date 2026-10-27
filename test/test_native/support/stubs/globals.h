#pragma once

#include "TinyLink.h"
#include "TinyTestAdapter.h"

/**
 * @brief Shared packed payload for loopback and duplex protocol tests.
 *
 * Packing keeps the transmitted representation at exactly eight bytes so
 * tests can detect accidental padding as well as float/field corruption.
 */
struct TestPayload {
    uint32_t uptime;
    float value;
} __attribute__((packed));

/**
 * @class LoopbackAdapter
 * @brief In-memory, single-end loopback transport used by shared native tests.
 *
 * Transmitted bytes are immediately injected into this adapter's RX buffer;
 * it intentionally does not model physical serial timing or flow control.
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

/** @brief Shared loopback transport; reset and drained by the Unity fixture. */
extern LoopbackAdapter adapter;
/** @brief Shared link under test, constructed over @ref adapter. */
extern tinylink::TinyLink<TestPayload, LoopbackAdapter> link;
/** @brief Callback result flag; setUp() clears it before each test. */
extern bool g_callbackTriggered;
/** @brief Minimal callback that records invocation in @ref g_callbackTriggered. */
extern void testCallback(const TestPayload& data);
