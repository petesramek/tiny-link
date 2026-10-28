#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#define ARDUINO
#include <adapters/TinyArduinoAdapter.h>
#include <TinyLink.h>

namespace {

    /** @brief Configurable Stream fixture for native adapter contract tests. */
    class FakeStream : public Stream {
    public:
        FakeStream()
            : availableBytes(0),
              nextRead(-1),
              singleWriteResult(1),
              bufferWriteResult(0),
              singleWrites(0),
              bufferWrites(0),
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
            const size_t accepted = bufferWriteResult == 0
                ? length
                : bufferWriteResult;
            if (data && accepted <= sizeof(written)) {
                for (size_t i = 0; i < accepted; ++i) {
                    written[i] = data[i];
                }
            }
            return accepted;
        }

        int availableBytes;
        int nextRead;
        size_t singleWriteResult; /**< Accepted byte count for single-byte writes. */
        size_t bufferWriteResult; /**< Buffer count override; zero reports full length. */
        size_t singleWrites;
        size_t bufferWrites;
        uint8_t lastSingleByte;
        size_t lastLength;
        uint8_t written[8];
    };

    static_assert(tinylink::is_valid_adapter<tinylink::TinyArduinoAdapter>::value,
        "Arduino adapter must satisfy TinyLink's transport contract");

    void testArduinoAdapterForwardsOperationsAndReportsWriteOutcomes()
    {
        FakeStream stream;
        tinylink::TinyArduinoAdapter adapter(stream);
        TEST_ASSERT_TRUE(adapter.isOpen());
        stream.availableBytes = 3;
        stream.nextRead = 0xE7;
        TEST_ASSERT_EQUAL_INT(3, adapter.available());
        TEST_ASSERT_EQUAL_INT(0xE7, adapter.read());

        const tinylink::WriteResult singleResult = adapter.write(0x42u);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Complete),
            static_cast<int>(singleResult.status));
        TEST_ASSERT_EQUAL_UINT32(1, singleResult.bytesWritten);
        const uint8_t bytes[] = { 1, 0, 2 };
        const tinylink::WriteResult bufferResult =
            adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Complete),
            static_cast<int>(bufferResult.status));
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), bufferResult.bytesWritten);
        TEST_ASSERT_EQUAL_UINT32(1, stream.singleWrites);
        TEST_ASSERT_EQUAL_UINT8(0x42u, stream.lastSingleByte);
        TEST_ASSERT_EQUAL_UINT32(1, stream.bufferWrites);
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), stream.lastLength);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, stream.written, sizeof(bytes));

        stream.bufferWriteResult = 2;
        const tinylink::WriteResult partial =
            adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Partial),
            static_cast<int>(partial.status));
        TEST_ASSERT_EQUAL_UINT32(2, partial.bytesWritten);

        stream.singleWriteResult = 0;
        const tinylink::WriteResult failed = adapter.write(0x43u);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::Failed),
            static_cast<int>(failed.status));
        TEST_ASSERT_EQUAL_UINT32(0, failed.bytesWritten);

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
        const tinylink::WriteResult empty = adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::WriteStatus::ZeroLength),
            static_cast<int>(empty.status));
        TEST_ASSERT_EQUAL_UINT32(0, stream.bufferWrites);
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
    RUN_TEST(testArduinoAdapterForwardsOperationsAndReportsWriteOutcomes);
    RUN_TEST(testArduinoAdapterHandlesEmptyReadAndZeroLengthWrite);
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
