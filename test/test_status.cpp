/**
 * @file test_status_strings.cpp
 * @brief Unit tests for TinyStatus and tinystatus_to_string (Status.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/Status.h"

using namespace tinylink;

/** @test STATUS_OK has wire value 0x00. */
void test_status_ok_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x00, static_cast<uint8_t>(TinyStatus::STATUS_OK));
}

/** @test ERR_CRC has wire value 0x01. */
void test_status_err_crc_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x01, static_cast<uint8_t>(TinyStatus::ERR_CRC));
}

/** @test ERR_TIMEOUT has wire value 0x02. */
void test_status_err_timeout_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x02, static_cast<uint8_t>(TinyStatus::ERR_TIMEOUT));
}

/** @test ERR_OVERFLOW has wire value 0x03. */
void test_status_err_overflow_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x03, static_cast<uint8_t>(TinyStatus::ERR_OVERFLOW));
}

/** @test ERR_BUSY has wire value 0x04. */
void test_status_err_busy_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x04, static_cast<uint8_t>(TinyStatus::ERR_BUSY));
}

/** @test ERR_PROCESSING has wire value 0x05. */
void test_status_err_processing_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0x05, static_cast<uint8_t>(TinyStatus::ERR_PROCESSING));
}

/** @test ERR_UNKNOWN has wire value 0xFF. */
void test_status_err_unknown_value(void) {
    TEST_ASSERT_EQUAL_HEX8(0xFF, static_cast<uint8_t>(TinyStatus::ERR_UNKNOWN));
}

/** @test tinystatus_to_string returns "OK" for STATUS_OK. */
void test_status_string_ok(void) {
    TEST_ASSERT_EQUAL_STRING("OK", tinystatus_to_string(TinyStatus::STATUS_OK));
}

/** @test tinystatus_to_string returns correct string for each defined code. */
void test_status_strings_all(void) {
    TEST_ASSERT_EQUAL_STRING("ERR_CRC",        tinystatus_to_string(TinyStatus::ERR_CRC));
    TEST_ASSERT_EQUAL_STRING("ERR_TIMEOUT",    tinystatus_to_string(TinyStatus::ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("ERR_OVERFLOW",   tinystatus_to_string(TinyStatus::ERR_OVERFLOW));
    TEST_ASSERT_EQUAL_STRING("ERR_BUSY",       tinystatus_to_string(TinyStatus::ERR_BUSY));
    TEST_ASSERT_EQUAL_STRING("ERR_PROCESSING", tinystatus_to_string(TinyStatus::ERR_PROCESSING));
    TEST_ASSERT_EQUAL_STRING("ERR_UNKNOWN",    tinystatus_to_string(TinyStatus::ERR_UNKNOWN));
}

/** @test tinystatus_to_string returns "INVALID" for an out-of-range value. */
void test_status_string_invalid(void) {
    TinyStatus bad = static_cast<TinyStatus>(0x7F);
    TEST_ASSERT_EQUAL_STRING("INVALID", tinystatus_to_string(bad));
}

void register_status_tests(void) {
    RUN_TEST(test_status_ok_value);
    RUN_TEST(test_status_err_crc_value);
    RUN_TEST(test_status_err_timeout_value);
    RUN_TEST(test_status_err_overflow_value);
    RUN_TEST(test_status_err_busy_value);
    RUN_TEST(test_status_err_processing_value);
    RUN_TEST(test_status_err_unknown_value);
    RUN_TEST(test_status_string_ok);
    RUN_TEST(test_status_strings_all);
    RUN_TEST(test_status_string_invalid);
}
