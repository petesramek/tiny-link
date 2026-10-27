#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "support/stubs/globals.h"

/**
 * @brief TEST: Sequence Tracking.
 * Verifies that packet sequence numbers correctly increment.
 */

void test_sequence_tracking(void) {
    TestPayload data = { 1, 1.0f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t seq1 = link.seq();

    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t seq2 = link.seq();

    TEST_ASSERT_EQUAL_UINT8(seq1 + 1, seq2);
}

/**
 * @brief TEST: Sequence Number Wrap-around.
 * Verifies that the uint8_t sequence counter correctly rolls over from 255 to 0
 * without losing synchronization or failing the Fletcher-16 check.
 */

void test_sequence_wrap_around(void) {
    TestPayload data = { 1, 1.0f };

    // 1. Get the starting sequence
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t startSeq = link.seq();

    // 2. Send 256 more packets to complete a full circle
    for(int i = 0; i < 256; i++) {
        link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), data);
        while(adapter.available() > 0) {
            link.update();
            if(link.available()) link.flush();
        }
    }

    // 3. It should have wrapped back to exactly startSeq
    TEST_ASSERT_EQUAL_UINT8(startSeq, link.seq());
}

void register_ack_sequence_tests(void) {
    RUN_TEST(test_sequence_tracking);
    RUN_TEST(test_sequence_wrap_around);
}
