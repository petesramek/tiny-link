#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#define ARDUINO
#include <adapters/TinyArduinoAdapter.h>
#include <TinyLink.h>

namespace {

    class FakeStream : public Stream {
    public:
        FakeStream()
            : availableBytes(0),
              nextRead(-1),
              singleWrites(0),
              bufferWrites(0),
              singleWriteResult(1),
              bufferWriteResult(3),
              lastSingleByte(0),
              lastLength(0)
        {}

        int available() { return availableBytes; }
        int read() { return nextRead; }
        size_t write(uint8_t value)
        {
            ++singleWrites;
            lastSingleByte = value;
            return singleWriteResult;
        }
        size_t write(const uint8_t* data, size_t length)
        {
            ++bufferWrites;
            lastLength = length;
            const size_t accepted = bufferWriteResult < length
                ? bufferWriteResult
                : length;
            if (data && accepted <= sizeof(written)) {
                for (size_t i = 0; i < accepted; ++i) {
                    written[i] = data[i];
                }
            }
            return bufferWriteResult;
        }

        int availableBytes;
        int nextRead;
        size_t singleWrites;
        size_t bufferWrites;
        size_t singleWriteResult;
        size_t bufferWriteResult;
        uint8_t lastSingleByte;
        size_t lastLength;
        uint8_t written[8];
    };

    static_assert(tinylink::is_valid_adapter<tinylink::TinyArduinoAdapter>::value,
        "Arduino adapter must satisfy TinyLink's transport contract");

    void testArduinoAdapterForwardsStreamOperationsAndClock()
    {
        FakeStream stream;
        tinylink::TinyArduinoAdapter adapter(stream);
        TEST_ASSERT_TRUE(adapter.isOpen());
        stream.availableBytes = 3;
        stream.nextRead = 0xE7;
        TEST_ASSERT_EQUAL_INT(3, adapter.available());
        TEST_ASSERT_EQUAL_INT(0xE7, adapter.read());

        const tinylink::TinyWriteResult singleResult = adapter.write(0x42u);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::Complete),
            static_cast<int>(singleResult.status));
        TEST_ASSERT_EQUAL_UINT32(1, singleResult.bytesWritten);
        const uint8_t bytes[] = { 1, 0, 2 };
        const tinylink::TinyWriteResult bufferResult =
            adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::Complete),
            static_cast<int>(bufferResult.status));
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), bufferResult.bytesWritten);
        TEST_ASSERT_EQUAL_UINT32(1, stream.singleWrites);
        TEST_ASSERT_EQUAL_UINT8(0x42u, stream.lastSingleByte);
        TEST_ASSERT_EQUAL_UINT32(1, stream.bufferWrites);
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), stream.lastLength);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, stream.written, sizeof(bytes));

        tinylinkArduinoStubMillis = 0x12345678ul;
        TEST_ASSERT_EQUAL_UINT32(0x12345678ul, adapter.millis());
    }

    void testArduinoAdapterHandlesEmptyReadAndZeroLengthWrite()
    {
        FakeStream stream;
        tinylink::TinyArduinoAdapter adapter(stream);
        stream.availableBytes = 0;
        stream.nextRead = -1;
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        const tinylink::TinyWriteResult zeroResult = adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::ZeroLength),
            static_cast<int>(zeroResult.status));
        TEST_ASSERT_EQUAL_UINT32(0, stream.bufferWrites);
    }

    void testArduinoAdapterPropagatesPartialAndFailedWrites()
    {
        FakeStream stream;
        tinylink::TinyArduinoAdapter adapter(stream);
        const uint8_t bytes[] = { 1, 2, 3 };

        stream.bufferWriteResult = 2;
        tinylink::TinyWriteResult result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::Partial),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(2, result.bytesWritten);

        stream.bufferWriteResult = 0;
        result = adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::Failed),
            static_cast<int>(result.status));
        TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);

        stream.singleWriteResult = 0;
        result = adapter.write(0x42u);
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(tinylink::TinyWriteStatus::Failed),
            static_cast<int>(result.status));
    }

    void testArduinoAdapterDoesNotOwnStream()
    {
        FakeStream stream;
        {
            tinylink::TinyArduinoAdapter adapter(stream);
        }
        stream.availableBytes = 7;
        TEST_ASSERT_EQUAL_INT(7, stream.available());
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testArduinoAdapterForwardsStreamOperationsAndClock);
    RUN_TEST(testArduinoAdapterHandlesEmptyReadAndZeroLengthWrite);
    RUN_TEST(testArduinoAdapterPropagatesPartialAndFailedWrites);
    RUN_TEST(testArduinoAdapterDoesNotOwnStream);
    return UNITY_END();
}

extern "C" void setUp(void)
{
    tinylinkArduinoStubMillis = 0;
}

extern "C" void tearDown(void)
{
}
