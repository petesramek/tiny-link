#include <unity.h>
#include <stdint.h>
#include <string.h>
#include "Packet.h"

void test_packet_roundtrip(void) {
    uint8_t payload[] = {1,2,3,4,5};
    uint8_t out[32];
    memset(out, 0, sizeof(out));

    size_t plen = tinylink::packet::pack('D', 7, payload, sizeof(payload), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_UINT32(0, plen);

    uint8_t type = 0, seq = 0;
    uint8_t recv[16];
    memset(recv, 0, sizeof(recv));

    size_t pLen = tinylink::packet::unpack(out, plen, &type, &seq, recv, sizeof(recv));
    TEST_ASSERT_EQUAL_UINT8('D', type);
    TEST_ASSERT_EQUAL_UINT8(7, seq);
    TEST_ASSERT_EQUAL_UINT32(sizeof(payload), pLen);
    TEST_ASSERT_EQUAL_INT8_ARRAY(payload, recv, pLen);
}

void test_packet_bad_checksum(void) {
    uint8_t payload[] = {9,9,9};
    uint8_t out[32];
    size_t plen = tinylink::packet::pack('X', 1, payload, sizeof(payload), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_UINT32(0, plen);

    // corrupt checksum
    out[plen - 1] ^= 0xFF;

    uint8_t type=0, seq=0;
    uint8_t recv[8];
    size_t pLen = tinylink::packet::unpack(out, plen, &type, &seq, recv, sizeof(recv));
    TEST_ASSERT_EQUAL_UINT32(0, pLen);
}

void test_packet_length_mismatch(void) {
    uint8_t payload[] = {10,11};
    uint8_t out[32];
    size_t plen = tinylink::packet::pack('Z', 3, payload, sizeof(payload), out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_UINT32(0, plen);

    // call unpack with truncated length
    uint8_t type=0, seq=0;
    uint8_t recv[8];
    size_t pLen = tinylink::packet::unpack(out, plen - 1, &type, &seq, recv, sizeof(recv));
    TEST_ASSERT_EQUAL_UINT32(0, pLen);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_packet_roundtrip);
    RUN_TEST(test_packet_bad_checksum);
    RUN_TEST(test_packet_length_mismatch);
    return UNITY_END();
}
