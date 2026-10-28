#include <unity.h>

#include "adapters/WriteResult.h"

namespace {

    void testWriteResultDistinguishesCompletePartialFailureAndEmpty()
    {
        const tinylink::WriteResult complete =
            tinylink::WriteResult::fromCount(4, 4);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Complete),
            static_cast<int>(complete.status));
        TEST_ASSERT_EQUAL_UINT32(4, complete.bytesWritten);
        TEST_ASSERT_TRUE(complete.complete());

        const tinylink::WriteResult partial =
            tinylink::WriteResult::fromCount(4, 2);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Partial),
            static_cast<int>(partial.status));
        TEST_ASSERT_EQUAL_UINT32(2, partial.bytesWritten);
        TEST_ASSERT_FALSE(partial.complete());

        const tinylink::WriteResult failed =
            tinylink::WriteResult::fromCount(4, 0);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(failed.status));
        TEST_ASSERT_EQUAL_UINT32(0, failed.bytesWritten);

        const tinylink::WriteResult empty =
            tinylink::WriteResult::fromCount(0, 0);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::ZeroLength),
            static_cast<int>(empty.status));
        TEST_ASSERT_EQUAL_UINT32(0, empty.bytesWritten);
        TEST_ASSERT_FALSE(empty.complete());
    }

    void testWriteResultRejectsImpossibleAcceptedByteCount()
    {
        const tinylink::WriteResult result =
            tinylink::WriteResult::fromCount(2, 3);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
    }

    void testWriteResultRepresentsPlatformFailure()
    {
        const tinylink::WriteResult result =
            tinylink::WriteResult::failure();
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
        TEST_ASSERT_FALSE(result.complete());
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testWriteResultDistinguishesCompletePartialFailureAndEmpty);
    RUN_TEST(testWriteResultRejectsImpossibleAcceptedByteCount);
    RUN_TEST(testWriteResultRepresentsPlatformFailure);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
