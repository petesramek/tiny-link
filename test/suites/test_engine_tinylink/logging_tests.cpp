#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

using namespace tinylink_test;

namespace {

void testValidLogFrameIsDecodedAndRoutedOnlyToLogCallback()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onLogReceived(onLog);
        tinylink::LogMessage message = {};
        message.uptimeMilliseconds = 0x12345678u;
        message.level = tinylink::LogLevel::WARN;
        message.code = 0xABCDu;
        tinylink::set_log_message_text(message, "transport warning");
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::encode_log_message(
                message, payload, sizeof(payload))));
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Log),
            77,
            payload,
            sizeof(payload)));
        link.update();

        TEST_ASSERT_EQUAL_UINT32(1, logCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(
            message.uptimeMilliseconds, lastLog.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(
            static_cast<int>(message.level), static_cast<int>(lastLog.level));
        TEST_ASSERT_EQUAL_UINT16(message.code, lastLog.code);
        TEST_ASSERT_EQUAL_STRING(message.text, lastLog.text);
        TEST_ASSERT_FALSE(link.available());
        TEST_ASSERT_FALSE(link.connected());
        TEST_ASSERT_EQUAL_UINT32(0, link.stats().packetsReceived);
    }

void testMalformedLogPayloadIsCountedAndNotDelivered()
    {
        FakeAdapter adapter;
        Link link(adapter);
        link.onLogReceived(onLog);
        uint8_t invalidLevel[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        invalidLevel[4] = 0xFFu;
        TEST_ASSERT_TRUE(tinylink_test::queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Log),
            0,
            invalidLevel,
            sizeof(invalidLevel)));
        link.update();
        TEST_ASSERT_EQUAL_UINT32(0, logCallbackCount);
        TEST_ASSERT_EQUAL_UINT32(1, link.stats().payloadSizeMismatches);
    }

void testSendLogEncodesFieldsAndUsesTimeAndSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        adapter.setTime(0x12345678ul);
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::ERROR, 0xABCDu, "native log"));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Log), type);
        TEST_ASSERT_EQUAL_UINT8(0, sequence);

        tinylink::LogMessage message = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_UINT32(0x12345678ul, message.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogLevel::ERROR),
            static_cast<int>(message.level));
        TEST_ASSERT_EQUAL_UINT16(0xABCDu, message.code);
        TEST_ASSERT_EQUAL_STRING("native log", message.text);

        const Payload application = payloadWith(0x61);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));
        TEST_ASSERT_TRUE(link.sendData(0x26, application));
        uint8_t appType = 0;
        uint8_t appSequence = 0;
        uint8_t appBytes[sizeof(Payload)] = {};
        size_t appLength = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter,
            tinylink_test::writtenFrameCount(adapter) - 1,
            appType, appSequence, appBytes, sizeof(appBytes), appLength));
        TEST_ASSERT_EQUAL_UINT8(1, appSequence);
    }

void testSendLogTruncatesLongTextAndNullTextBecomesEmpty()
    {
        FakeAdapter adapter;
        Link link(adapter);
        const char longText[] = "123456789012345678901234567890";
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::INFO, 1, longText));
        TEST_ASSERT_TRUE(link.sendLog(
            tinylink::LogLevel::INFO, 2, nullptr));

        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        size_t payloadLength = 0;
        tinylink::LogMessage message = {};
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_STRING("123456789012345678901234", message.text);

        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 1, type, sequence, payload, sizeof(payload), payloadLength));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                payload, payloadLength, message)));
        TEST_ASSERT_EQUAL_STRING("", message.text);
    }

void testSendLogPropagatesWriteFailureWithoutConsumingSequence()
    {
        FakeAdapter adapter;
        Link link(adapter);
        TEST_ASSERT_TRUE(connectPeer(adapter, link));

        const size_t failed[] = { 0 };
        scriptWrites(adapter, failed, 1);
        TEST_ASSERT_FALSE(link.sendLog(tinylink::LogLevel::INFO, 7, "x"));
        TEST_ASSERT_EQUAL_UINT32(0, adapter.outputSize());

        const size_t noDelimiter[] = { ALL, 0 };
        scriptWrites(adapter, noDelimiter, 2);
        TEST_ASSERT_FALSE(link.sendLog(tinylink::LogLevel::INFO, 7, "x"));
        adapter.clearOutput();

        TEST_ASSERT_TRUE(link.sendLog(tinylink::LogLevel::INFO, 7, "x"));
        // The stale body is terminated first, so the log is the second frame.
        TEST_ASSERT_EQUAL_UINT8(0, sentSequence(adapter, 1));
    }

} // namespace

void registerLoggingTests()
{
    RUN_TEST(testValidLogFrameIsDecodedAndRoutedOnlyToLogCallback);
    RUN_TEST(testMalformedLogPayloadIsCountedAndNotDelivered);
    RUN_TEST(testSendLogEncodesFieldsAndUsesTimeAndSequence);
    RUN_TEST(testSendLogTruncatesLongTextAndNullTextBecomesEmpty);
    RUN_TEST(testSendLogPropagatesWriteFailureWithoutConsumingSequence);
}
