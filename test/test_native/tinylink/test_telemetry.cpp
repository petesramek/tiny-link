#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/** @test Engine packet telemetry saturates at UINT32_MAX instead of wrapping. */

void test_engine_packet_counter_saturates(void) {
    const tinylink::TinyStats::CountType maximum =
        static_cast<tinylink::TinyStats::CountType>(-1);
    tinylink::TinyStats& stats =
        const_cast<tinylink::TinyStats&>(link.getStats());
    stats.packets = maximum - 1;

    TestPayload data = { 7, 7.0f };
    TEST_ASSERT_TRUE(link.sendData(
        static_cast<uint8_t>(tinylink::MessageType::Data), data));
    while (adapter.available() > 0 && !link.available()) link.update();
    TEST_ASSERT_EQUAL_UINT32(maximum, link.getStats().packets);
    link.flush();

    TEST_ASSERT_TRUE(link.sendData(
        static_cast<uint8_t>(tinylink::MessageType::Data), data));
    while (adapter.available() > 0 && !link.available()) link.update();
    TEST_ASSERT_EQUAL_UINT32(maximum, link.getStats().packets);
}

void register_telemetry_tests(void) {
    RUN_TEST(test_engine_packet_counter_saturates);
}
