#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#define ESP_PLATFORM
#include <adapters/TinyESPIDFAdapter.h>
#include <TinyLink.h>

namespace {

    static_assert(tinylink::is_valid_adapter<tinylink::TinyESPIDFAdapter>::value,
        "ESP-IDF adapter must satisfy TinyLink's transport contract");

    tinylink::TinyESPIDFAdapter adapter(UART_NUM_0);

    void testEspIdfAdapterChecksDriverAvailabilityAndBufferedBytes()
    {
        TEST_ASSERT_TRUE(adapter.isOpen());
        tinylinkEspBufferedSize = 9;
        TEST_ASSERT_EQUAL_INT(9, adapter.available());
        tinylinkEspBufferStatus = ESP_FAIL;
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        tinylinkEspBufferStatus = ESP_OK;
        tinylinkEspDriverInstalled = false;
        TEST_ASSERT_FALSE(adapter.isOpen());
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());

        tinylinkEspDriverInstalled = true;
        tinylinkEspBufferedSize = static_cast<size_t>(INT_MAX) + 1u;
        TEST_ASSERT_EQUAL_INT(INT_MAX, adapter.available());
    }

    void testEspIdfAdapterReadsBytesAndMapsEmptyOrErrorToMinusOne()
    {
        tinylinkEspReadValue = 0xE9u;
        tinylinkEspReadResult = 1;
        TEST_ASSERT_EQUAL_INT(0xE9, adapter.read());
        tinylinkEspReadResult = 0;
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        tinylinkEspReadResult = -1;
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
    }

    void testEspIdfAdapterReportsCompletePartialFailedAndEmptyWrites()
    {
        const uint8_t bytes[] = { 0x11u, 0x00u, 0xFEu };
        const size_t callsBefore = tinylinkEspWriteCalls;
        tinylink::WriteResult result = adapter.write(0xA5u);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Complete),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(callsBefore + 1, tinylinkEspWriteCalls);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkEspLastWriteLength);
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Complete),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), result.bytesWritten);
        TEST_ASSERT_EQUAL_UINT32(callsBefore + 2, tinylinkEspWriteCalls);
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), tinylinkEspLastWriteLength);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            bytes, tinylinkEspLastWriteBytes, sizeof(bytes));

        tinylinkEspWriteResultOverridden = true;
        tinylinkEspWriteResult = 2;
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Partial),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(2, result.bytesWritten);
        tinylinkEspWriteResult = -1;
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        tinylinkEspWriteResult = 0;
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        const size_t callsBeforeEmpty = tinylinkEspWriteCalls;
        result = adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::ZeroLength),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(callsBeforeEmpty, tinylinkEspWriteCalls);

        adapter.write(nullptr, 1);
        adapter.write(bytes, static_cast<size_t>(INT_MAX) + 1u);
        TEST_ASSERT_EQUAL_UINT32(callsBefore + 5, tinylinkEspWriteCalls);

        tinylinkEspDriverInstalled = false;
        result = adapter.write(1);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(callsBefore + 5, tinylinkEspWriteCalls);
    }

    void testEspIdfAdapterConvertsMicrosecondsToMilliseconds()
    {
        tinylinkEspStubTimeMicroseconds = 12345678;
        TEST_ASSERT_EQUAL_UINT32(12345, adapter.millis());
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testEspIdfAdapterChecksDriverAvailabilityAndBufferedBytes);
    RUN_TEST(testEspIdfAdapterReadsBytesAndMapsEmptyOrErrorToMinusOne);
    RUN_TEST(testEspIdfAdapterReportsCompletePartialFailedAndEmptyWrites);
    RUN_TEST(testEspIdfAdapterConvertsMicrosecondsToMilliseconds);
    return UNITY_END();
}

extern "C" void setUp(void)
{
    tinylinkEspDriverInstalled = true;
    tinylinkEspBufferStatus = ESP_OK;
    tinylinkEspBufferedSize = 0;
    tinylinkEspReadResult = 0;
    tinylinkEspReadValue = 0;
    tinylinkEspWriteCalls = 0;
    tinylinkEspLastWriteLength = 0;
    tinylinkEspWriteResultOverridden = false;
    tinylinkEspWriteResult = 0;
    tinylinkEspStubTimeMicroseconds = 0;
}

extern "C" void tearDown(void)
{
}
