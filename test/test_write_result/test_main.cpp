/**
 * @file test_write_result.cpp
 * @brief Unit tests for transport adapter write result classification.
 */

#include <unity.h>
#include "TinyWriteResult.h"

using namespace tinylink;

void test_write_result_complete(void)
{
    const TinyWriteResult result = tiny_write_result(4, 4, true);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TinyWriteStatus::Complete),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(4, result.bytesWritten);
}

void test_write_result_partial(void)
{
    const TinyWriteResult result = tiny_write_result(4, 2, false);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TinyWriteStatus::Partial),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(2, result.bytesWritten);
}

void test_write_result_failed(void)
{
    const TinyWriteResult result = tiny_write_result(4, 0, false);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TinyWriteStatus::Failed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
}

void test_write_result_zero_length(void)
{
    const TinyWriteResult result = tiny_write_result(0, 0, true);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(TinyWriteStatus::ZeroLength),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_result_complete);
    RUN_TEST(test_write_result_partial);
    RUN_TEST(test_write_result_failed);
    RUN_TEST(test_write_result_zero_length);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
