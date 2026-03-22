/**
 * @file test_ackmessage_more.cpp
 * @brief Additional unit tests for TinyAck (AckMessage.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/AckMessage.h"

using namespace tinylink;

/** @test All TinyStatus codes survive a TinyAck round-trip. */
void test_ackmessage_all_status_roundtrip(void) {
    static const TinyStatus codes[] = {
        TinyStatus::STATUS_OK,
        TinyStatus::ERR_CRC,
        TinyStatus::ERR_TIMEOUT,
        TinyStatus::ERR_OVERFLOW,
        TinyStatus::ERR_BUSY,
        TinyStatus::ERR_PROCESSING,
        TinyStatus::ERR_UNKNOWN
    };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i) {
        TinyAck a;
        a.seq    = static_cast<uint8_t>(i);
        a.result = codes[i];

        uint8_t buf[2];
        memcpy(buf, &a, 2);

        TinyAck b;
        memcpy(&b, buf, 2);

        TEST_ASSERT_EQUAL_UINT8(a.seq, b.seq);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(a.result),
                                static_cast<uint8_t>(b.result));
    }
}

/** @test Sequence number 0 is preserved. */
void test_ackmessage_seq_zero(void) {
    TinyAck a;
    a.seq    = 0;
    a.result = TinyStatus::STATUS_OK;

    uint8_t buf[2];
    memcpy(buf, &a, 2);

    TinyAck b;
    memcpy(&b, buf, 2);
    TEST_ASSERT_EQUAL_UINT8(0, b.seq);
}

/** @test Sequence number 255 is preserved. */
void test_ackmessage_seq_max(void) {
    TinyAck a;
    a.seq    = 0xFF;
    a.result = TinyStatus::ERR_UNKNOWN;

    uint8_t buf[2];
    memcpy(buf, &a, 2);

    TinyAck b;
    memcpy(&b, buf, 2);
    TEST_ASSERT_EQUAL_UINT8(0xFF, b.seq);
    TEST_ASSERT_EQUAL_HEX8(0xFF, static_cast<uint8_t>(b.result));
}

/** @test ERR_UNKNOWN wire value is 0xFF. */
void test_ackmessage_err_unknown_wire_value(void) {
    TinyAck a;
    a.seq    = 1;
    a.result = TinyStatus::ERR_UNKNOWN;

    uint8_t buf[2];
    memcpy(buf, &a, 2);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
}

void register_ackmessage_more_tests(void) {
    RUN_TEST(test_ackmessage_all_status_roundtrip);
    RUN_TEST(test_ackmessage_seq_zero);
    RUN_TEST(test_ackmessage_seq_max);
    RUN_TEST(test_ackmessage_err_unknown_wire_value);
}
