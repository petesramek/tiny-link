#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#include "../test_tinylink/TinyLinkTestHelpers.h"

namespace {

    struct Payload {
        uint8_t bytes[4];
    };

    class WriteResultAdapter {
    public:
        WriteResultAdapter()
            : _failNext(false),
              _acceptedBeforeFailure(0)
        {}

        bool isOpen() const { return _adapter.isOpen(); }
        int available() const { return _adapter.available(); }
        int read() { return _adapter.read(); }
        unsigned long millis() const { return _adapter.millis(); }

        tinylink::TinyWriteResult write(uint8_t value)
        {
            return write(&value, 1);
        }

        tinylink::TinyWriteResult write(const uint8_t* data, size_t length)
        {
            const size_t before = _adapter.outputSize();
            if (_failNext) {
                _failNext = false;
                const size_t accepted =
                    _acceptedBeforeFailure == static_cast<size_t>(-1)
                        ? length - 1u
                        : (_acceptedBeforeFailure < length
                            ? _acceptedBeforeFailure
                            : length);
                if (accepted > 0) {
                    _adapter.write(data, accepted);
                }
                return tinylink::tiny_write_result(length, accepted, false);
            }

            _adapter.write(data, length);
            const size_t written = _adapter.outputSize() - before;
            return tinylink::tiny_write_result(
                length, written, written == length);
        }

        void failNextWriteAfter(size_t accepted)
        {
            _failNext = true;
            _acceptedBeforeFailure = accepted;
        }

        tinylink_test::FakeAdapter& fake() { return _adapter; }

    private:
        tinylink_test::FakeAdapter _adapter;
        bool _failNext;
        size_t _acceptedBeforeFailure;
    };

    typedef tinylink::TinyLink<Payload, WriteResultAdapter> Link;

    bool connect(Link& link, WriteResultAdapter& adapter)
    {
        if (!link.begin()) {
            return false;
        }
        adapter.fake().clearOutput();
        if (!tinylink_test::queueHandshake(
                adapter.fake(), tinylink::HandshakeKind::Response)) {
            return false;
        }
        link.update();
        adapter.fake().clearOutput();
        return link.connected();
    }

    Payload payload()
    {
        const Payload value = {{1, 2, 3, 4}};
        return value;
    }

    uint8_t sentSequence(WriteResultAdapter& adapter)
    {
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t bytes[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t length = 0;
        if (!tinylink_test::decodeWrittenFrame(
                adapter.fake(), 0, type, sequence,
                bytes, sizeof(bytes), length)) {
            return 255;
        }
        return sequence;
    }

    void testCompleteDataWriteSucceedsAndIncludesDelimiter()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));

        TEST_ASSERT_TRUE(link.sendData(0x31, payload()));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter));
        TEST_ASSERT_EQUAL_UINT8(
            0, adapter.fake().output()[adapter.fake().outputSize() - 1]);
        TEST_ASSERT_TRUE(link.awaitingAck());
    }

    void testFailedDataWriteDoesNotConsumeSequenceOrAwaitAck()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));
        adapter.failNextWriteAfter(0);

        TEST_ASSERT_FALSE(link.sendData(0x31, payload()));
        TEST_ASSERT_FALSE(link.awaitingAck());

        adapter.fake().clearOutput();
        TEST_ASSERT_TRUE(link.sendData(0x31, payload()));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter));
    }

    void testPartialDataWriteDoesNotConsumeSequenceOrAwaitAck()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));
        adapter.failNextWriteAfter(3);

        TEST_ASSERT_FALSE(link.sendData(0x31, payload()));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_EQUAL_UINT(3, adapter.fake().outputSize());

        adapter.fake().clearOutput();
        TEST_ASSERT_TRUE(link.sendData(0x31, payload()));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter));
    }

    void testWriteMissingFinalDelimiterIsIncomplete()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));
        adapter.failNextWriteAfter(static_cast<size_t>(-1));

        TEST_ASSERT_FALSE(link.sendData(0x31, payload()));
        TEST_ASSERT_FALSE(link.awaitingAck());
        TEST_ASSERT_GREATER_THAN_UINT(0, adapter.fake().outputSize());

        adapter.fake().clearOutput();
        TEST_ASSERT_TRUE(link.sendData(0x31, payload()));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter));
    }

    void testAckWriteFailureIsReported()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));
        TEST_ASSERT_TRUE(tinylink_test::queueApplication(
            adapter.fake(), 0x31, 7, payload().bytes, sizeof(Payload)));
        link.update();
        adapter.failNextWriteAfter(0);

        TEST_ASSERT_FALSE(link.sendAck());
    }

    void testLogWriteFailureDoesNotConsumeSequence()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connect(link, adapter));
        adapter.failNextWriteAfter(0);

        TEST_ASSERT_FALSE(link.sendLog(
            tinylink::LogLevel::INFO, 42, "failed"));
        adapter.fake().clearOutput();
        TEST_ASSERT_TRUE(link.sendData(0x31, payload()));
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter));
    }

    void testBeginReportsHandshakeWriteFailure()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        adapter.failNextWriteAfter(0);

        TEST_ASSERT_FALSE(link.begin());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Connecting),
            static_cast<int>(link.connectionState()));
    }

    void testRequestDoesNotConnectWhenResponseWriteFails()
    {
        WriteResultAdapter adapter;
        Link link(adapter);
        adapter.failNextWriteAfter(0);
        TEST_ASSERT_TRUE(tinylink_test::queueHandshake(
            adapter.fake(), tinylink::HandshakeKind::Request));

        link.update();

        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::ConnectionState::Inactive),
            static_cast<int>(link.connectionState()));
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testCompleteDataWriteSucceedsAndIncludesDelimiter);
    RUN_TEST(testFailedDataWriteDoesNotConsumeSequenceOrAwaitAck);
    RUN_TEST(testPartialDataWriteDoesNotConsumeSequenceOrAwaitAck);
    RUN_TEST(testWriteMissingFinalDelimiterIsIncomplete);
    RUN_TEST(testAckWriteFailureIsReported);
    RUN_TEST(testLogWriteFailureDoesNotConsumeSequence);
    RUN_TEST(testBeginReportsHandshakeWriteFailure);
    RUN_TEST(testRequestDoesNotConnectWhenResponseWriteFails);
    return UNITY_END();
}

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}
