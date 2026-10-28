#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "Version.h"
#include "internal/protocol/AckMessage.h"
#include "internal/protocol/HandshakeMessage.h"
#include "internal/protocol/LogMessage.h"
#include "protocol/AckResult.h"
#include "protocol/LogLevel.h"
#include "protocol/MessageType.h"
#include "protocol/State.h"
#include "support/tinylink/TestDiagnostics.h"

namespace {

    void testAckResultWireValuesStringsAndInvalidConversions()
    {
        const tinylink::AckResult results[] = {
            tinylink::AckResult::Ok,
            tinylink::AckResult::Busy,
            tinylink::AckResult::Unknown
        };
        const uint8_t wire[] = { 0x00u, 0x04u, 0xFFu };
        const char* names[] = { "Ok", "Busy", "Unknown" };

        for (size_t i = 0; i < 3; ++i) {
            tinylink::AckResult decoded = tinylink::AckResult::Busy;
            const char* context = tinylink_test::diag(
                "ack result=%s wire=0x%02X index=%u",
                names[i], (unsigned)wire[i], (unsigned)i);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                wire[i], tinylink::ack_result_to_wire(results[i]), context);
            TEST_ASSERT_TRUE_MESSAGE(
                tinylink::ack_result_from_wire(wire[i], decoded), context);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(results[i]), static_cast<int>(decoded), context);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                names[i], tinylink::ack_result_to_string(results[i]), context);
        }

        tinylink::AckResult unchanged = tinylink::AckResult::Busy;
        TEST_ASSERT_FALSE(tinylink::ack_result_from_wire(0x01u, unchanged));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Busy),
            static_cast<int>(unchanged));
        TEST_ASSERT_EQUAL_STRING(
            "Invalid",
            tinylink::ack_result_to_string(static_cast<tinylink::AckResult>(0x7Fu)));
    }

    void testAckMessageCodecRoundTripsAllDeclaredResults()
    {
        const tinylink::AckResult results[] = {
            tinylink::AckResult::Ok,
            tinylink::AckResult::Busy,
            tinylink::AckResult::Unknown
        };
        const uint8_t wire[] = { 0x00u, 0x04u, 0xFFu };

        for (size_t i = 0; i < 3; ++i) {
            uint8_t output[tinylink::ACK_MESSAGE_WIRE_SIZE] = { 0xA5u };
            const char* context = tinylink_test::diag(
                "ack round trip wire=0x%02X index=%u", (unsigned)wire[i], (unsigned)i);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(tinylink::AckMessageCodecStatus::Success),
                static_cast<int>(tinylink::encode_ack_message(
                    results[i], output, sizeof(output))),
                context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(wire[i], output[0], context);

            tinylink::AckResult decoded = tinylink::AckResult::Unknown;
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(tinylink::AckMessageCodecStatus::Success),
                static_cast<int>(tinylink::decode_ack_message(
                    output, sizeof(output), decoded)),
                context);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(results[i]), static_cast<int>(decoded), context);
        }
    }

    void testAckCodecErrorsPreserveDestinationsAndOutput()
    {
        uint8_t output[] = { 0xA1u, 0xA2u };
        const uint8_t original[] = { 0xA1u, 0xA2u };
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::encode_ack_message(
                tinylink::AckResult::Ok, nullptr, 1)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::OutputTooSmall),
            static_cast<int>(tinylink::encode_ack_message(
                tinylink::AckResult::Ok, output, 0)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidResult),
            static_cast<int>(tinylink::encode_ack_message(
                static_cast<tinylink::AckResult>(0x01u), output, sizeof(output))));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original, output, sizeof(output));

        const uint8_t invalid[] = { 0x02u, 0x00u };
        tinylink::AckResult result = tinylink::AckResult::Busy;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::decode_ack_message(nullptr, 1, result)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidLength),
            static_cast<int>(tinylink::decode_ack_message(invalid, 0, result)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidLength),
            static_cast<int>(tinylink::decode_ack_message(
                invalid, sizeof(invalid), result)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckMessageCodecStatus::InvalidResult),
            static_cast<int>(tinylink::decode_ack_message(invalid, 1, result)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::AckResult::Busy),
            static_cast<int>(result));
    }

    void testHandshakeKindsAndWireValuesAreStable()
    {
        tinylink::HandshakeKind kind = tinylink::HandshakeKind::Response;
        TEST_ASSERT_TRUE(tinylink::is_valid_handshake_kind(
            tinylink::HandshakeKind::Request));
        TEST_ASSERT_TRUE(tinylink::is_valid_handshake_kind(
            tinylink::HandshakeKind::Response));
        TEST_ASSERT_FALSE(tinylink::is_valid_handshake_kind(
            static_cast<tinylink::HandshakeKind>(0x7Fu)));

        TEST_ASSERT_TRUE(tinylink::handshake_kind_from_wire(0x00u, kind));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Request),
            static_cast<int>(kind));
        TEST_ASSERT_TRUE(tinylink::handshake_kind_from_wire(0x01u, kind));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Response),
            static_cast<int>(kind));
        TEST_ASSERT_FALSE(tinylink::handshake_kind_from_wire(0x02u, kind));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Response),
            static_cast<int>(kind));
    }

    void testHandshakeCodecRoundTripsRequestAndResponse()
    {
        const tinylink::HandshakeKind kinds[] = {
            tinylink::HandshakeKind::Request,
            tinylink::HandshakeKind::Response
        };

        for (size_t i = 0; i < 2; ++i) {
            const tinylink::HandshakeMessage source = {
                kinds[i], TINYLINK_PROTOCOL_VERSION
            };
            const char* context = tinylink_test::diag(
                "handshake kind=%u index=%u",
                (unsigned)static_cast<uint8_t>(kinds[i]), (unsigned)i);
            uint8_t output[tinylink::HANDSHAKE_MESSAGE_WIRE_SIZE] = { 0xA5u, 0xA6u };
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(tinylink::HandshakeMessageCodecStatus::Success),
                static_cast<int>(tinylink::encode_handshake_message(
                    source, output, sizeof(output))),
                context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                static_cast<uint8_t>(kinds[i]), output[0], context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                TINYLINK_PROTOCOL_VERSION, output[1], context);

            tinylink::HandshakeMessage decoded = {};
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(tinylink::HandshakeMessageCodecStatus::Success),
                static_cast<int>(tinylink::decode_handshake_message(
                    output, sizeof(output), decoded)),
                context);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(source.kind), static_cast<int>(decoded.kind), context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                source.protocolVersion, decoded.protocolVersion, context);
        }
    }

    void testHandshakeCodecRejectsInvalidInputsWithoutMutation()
    {
        uint8_t output[] = { 0xA1u, 0xA2u, 0xA3u };
        const uint8_t original[] = { 0xA1u, 0xA2u, 0xA3u };
        tinylink::HandshakeMessage source = {
            tinylink::HandshakeKind::Request, TINYLINK_PROTOCOL_VERSION
        };
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::encode_handshake_message(source, nullptr, 2)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::OutputTooSmall),
            static_cast<int>(tinylink::encode_handshake_message(source, output, 1)));
        source.kind = static_cast<tinylink::HandshakeKind>(0x7Fu);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::InvalidKind),
            static_cast<int>(tinylink::encode_handshake_message(
                source, output, sizeof(output))));
        source.kind = tinylink::HandshakeKind::Request;
        source.protocolVersion = static_cast<uint8_t>(TINYLINK_PROTOCOL_VERSION + 1);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::UnsupportedVersion),
            static_cast<int>(tinylink::encode_handshake_message(
                source, output, sizeof(output))));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(original, output, sizeof(output));

        const uint8_t invalidKind[] = { 0x7Fu, TINYLINK_PROTOCOL_VERSION };
        const uint8_t unsupportedVersion[] = {
            static_cast<uint8_t>(tinylink::HandshakeKind::Response),
            static_cast<uint8_t>(TINYLINK_PROTOCOL_VERSION + 1)
        };
        tinylink::HandshakeMessage decoded = {
            tinylink::HandshakeKind::Response, 0xA5u
        };
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::decode_handshake_message(nullptr, 2, decoded)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::InvalidLength),
            static_cast<int>(tinylink::decode_handshake_message(
                invalidKind, 1, decoded)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::InvalidKind),
            static_cast<int>(tinylink::decode_handshake_message(
                invalidKind, sizeof(invalidKind), decoded)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeMessageCodecStatus::UnsupportedVersion),
            static_cast<int>(tinylink::decode_handshake_message(
                unsupportedVersion, sizeof(unsupportedVersion), decoded)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::HandshakeKind::Response),
            static_cast<int>(decoded.kind));
        TEST_ASSERT_EQUAL_UINT8(0xA5u, decoded.protocolVersion);
    }

    void testLogTextSetterHandlesNullEmptyExactAndTruncatedStrings()
    {
        tinylink::LogMessage message = {};
        memset(message.text, 'x', sizeof(message.text));
        tinylink::set_log_message_text(message, nullptr);
        TEST_ASSERT_EQUAL_STRING("", message.text);
        for (size_t i = 0; i < sizeof(message.text); ++i) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                0, message.text[i],
                tinylink_test::diag("text byte not cleared at offset=%u of %u",
                    (unsigned)i, (unsigned)sizeof(message.text)));
        }

        tinylink::set_log_message_text(message, "short");
        TEST_ASSERT_EQUAL_STRING("short", message.text);
        TEST_ASSERT_EQUAL_UINT8(0, message.text[5]);

        const char maximum[] = "123456789012345678901234";
        tinylink::set_log_message_text(message, maximum);
        TEST_ASSERT_EQUAL_STRING(maximum, message.text);
        TEST_ASSERT_EQUAL_UINT8(0, message.text[tinylink::LOG_MESSAGE_MAX_TEXT_LENGTH]);

        const char longer[] = "1234567890123456789012345tail";
        tinylink::set_log_message_text(message, longer);
        TEST_ASSERT_EQUAL_STRING("123456789012345678901234", message.text);
        TEST_ASSERT_EQUAL_UINT8(0, message.text[tinylink::LOG_MESSAGE_TEXT_CAPACITY - 1]);
    }

    void testLogCodecUsesDocumentedLittleEndianWireLayoutAndRoundTrips()
    {
        tinylink::LogMessage message = {};
        message.uptimeMilliseconds = 0x78563412u;
        message.level = tinylink::LogLevel::ERROR;
        message.code = 0xABCDu;
        tinylink::set_log_message_text(message, "wire");
        uint8_t output[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::encode_log_message(
                message, output, sizeof(output))));

        const uint8_t expectedPrefix[] = {
            0x12u, 0x34u, 0x56u, 0x78u,
            static_cast<uint8_t>(tinylink::LogLevel::ERROR),
            0xCDu, 0xABu, 'w', 'i', 'r', 'e', 0
        };
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPrefix, output, sizeof(expectedPrefix));
        for (size_t i = 12; i < sizeof(output); ++i) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                0, output[i],
                tinylink_test::diag("log padding byte nonzero at wire offset=%u of %u",
                    (unsigned)i, (unsigned)sizeof(output)));
        }

        tinylink::LogMessage decoded = {};
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::decode_log_message(
                output, sizeof(output), decoded)));
        TEST_ASSERT_EQUAL_UINT32(message.uptimeMilliseconds, decoded.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(static_cast<int>(message.level), static_cast<int>(decoded.level));
        TEST_ASSERT_EQUAL_UINT16(message.code, decoded.code);
        TEST_ASSERT_EQUAL_STRING(message.text, decoded.text);
    }

    void testLogCodecRejectsInvalidCapacityLevelLengthAndTextWithoutMutation()
    {
        tinylink::LogMessage message = {};
        message.uptimeMilliseconds = 1;
        message.level = tinylink::LogLevel::INFO;
        message.code = 2;
        tinylink::set_log_message_text(message, "kept");
        uint8_t output[tinylink::LOG_MESSAGE_WIRE_SIZE + 1];
        memset(output, 0xA5, sizeof(output));

        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::encode_log_message(message, nullptr, sizeof(output))));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::Success),
            static_cast<int>(tinylink::encode_log_message(message, output, sizeof(output) - 1)));
        memset(output, 0xA5, sizeof(output));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::OutputTooSmall),
            static_cast<int>(tinylink::encode_log_message(message, output, sizeof(output) - 2)));
        message.level = static_cast<tinylink::LogLevel>(0xFFu);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::InvalidLevel),
            static_cast<int>(tinylink::encode_log_message(message, output, sizeof(output))));
        message.level = tinylink::LogLevel::INFO;
        memset(message.text, 'x', sizeof(message.text));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::UnterminatedText),
            static_cast<int>(tinylink::encode_log_message(message, output, sizeof(output))));
        for (size_t i = 0; i < sizeof(output); ++i) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                0xA5u, output[i],
                tinylink_test::diag("output mutated after failed encode at offset=%u of %u",
                    (unsigned)i, (unsigned)sizeof(output)));
        }

        uint8_t invalid[tinylink::LOG_MESSAGE_WIRE_SIZE] = {};
        tinylink::LogMessage decoded = {};
        decoded.uptimeMilliseconds = 0x12345678u;
        decoded.level = tinylink::LogLevel::WARN;
        decoded.code = 0xABCDu;
        tinylink::set_log_message_text(decoded, "unchanged");
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::InvalidArgument),
            static_cast<int>(tinylink::decode_log_message(nullptr, sizeof(invalid), decoded)));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::InvalidLength),
            static_cast<int>(tinylink::decode_log_message(
                invalid, sizeof(invalid) - 1, decoded)));
        invalid[4] = 0xFFu;
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::InvalidLevel),
            static_cast<int>(tinylink::decode_log_message(
                invalid, sizeof(invalid), decoded)));
        invalid[4] = static_cast<uint8_t>(tinylink::LogLevel::INFO);
        memset(invalid + 7, 'x', tinylink::LOG_MESSAGE_TEXT_CAPACITY);
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogMessageCodecStatus::UnterminatedText),
            static_cast<int>(tinylink::decode_log_message(
                invalid, sizeof(invalid), decoded)));
        TEST_ASSERT_EQUAL_UINT32(0x12345678u, decoded.uptimeMilliseconds);
        TEST_ASSERT_EQUAL(static_cast<int>(tinylink::LogLevel::WARN),
            static_cast<int>(decoded.level));
        TEST_ASSERT_EQUAL_UINT16(0xABCDu, decoded.code);
        TEST_ASSERT_EQUAL_STRING("unchanged", decoded.text);
    }

    void testLogLevelValuesConversionsThresholdsAndDiagnostics()
    {
        const tinylink::LogLevel levels[] = {
            tinylink::LogLevel::TRACE,
            tinylink::LogLevel::DEBUG,
            tinylink::LogLevel::INFO,
            tinylink::LogLevel::WARN,
            tinylink::LogLevel::ERROR
        };
        const char* names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };

        for (uint8_t i = 0; i < 5; ++i) {
            tinylink::LogLevel decoded = tinylink::LogLevel::ERROR;
            const char* context = tinylink_test::diag(
                "log level=%s wire=%u", names[i], (unsigned)i);
            TEST_ASSERT_TRUE_MESSAGE(tinylink::is_valid_log_level(i), context);
            TEST_ASSERT_TRUE_MESSAGE(tinylink::is_valid_log_level(levels[i]), context);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                i, tinylink::log_level_to_wire(levels[i]), context);
            TEST_ASSERT_TRUE_MESSAGE(tinylink::log_level_from_wire(i, decoded), context);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(levels[i]), static_cast<int>(decoded), context);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                names[i], tinylink::log_level_to_string(levels[i]), context);
            for (uint8_t minimum = 0; minimum < 5; ++minimum) {
                TEST_ASSERT_EQUAL_MESSAGE(
                    i >= minimum,
                    tinylink::log_level_meets_threshold(levels[i], levels[minimum]),
                    tinylink_test::diag("threshold level=%s minimum=%s",
                        names[i], names[minimum]));
            }
        }

        tinylink::LogLevel unchanged = tinylink::LogLevel::WARN;
        TEST_ASSERT_FALSE(tinylink::is_valid_log_level(5));
        TEST_ASSERT_FALSE(tinylink::is_valid_log_level(
            static_cast<tinylink::LogLevel>(0xFFu)));
        TEST_ASSERT_FALSE(tinylink::log_level_from_wire(0xFFu, unchanged));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::LogLevel::WARN),
            static_cast<int>(unchanged));
        TEST_ASSERT_FALSE(tinylink::log_level_meets_threshold(
            static_cast<tinylink::LogLevel>(0xFFu), tinylink::LogLevel::TRACE));
        TEST_ASSERT_EQUAL_STRING(
            "INVALID",
            tinylink::log_level_to_string(static_cast<tinylink::LogLevel>(0xFFu)));
    }

    void testMessageTypeValuesReservedConversionsAndDiagnostics()
    {
        const tinylink::MessageType types[] = {
            tinylink::MessageType::Log,
            tinylink::MessageType::Ack,
            tinylink::MessageType::Handshake
        };
        const uint8_t wire[] = { 'L', 'A', 'H' };
        const char* names[] = { "Log", "Ack", "Handshake" };

        for (size_t i = 0; i < 3; ++i) {
            tinylink::MessageType decoded = tinylink::MessageType::Log;
            const char* context = tinylink_test::diag(
                "message type=%s wire='%c' index=%u", names[i], (char)wire[i], (unsigned)i);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(
                wire[i], tinylink::message_type_to_wire(types[i]), context);
            TEST_ASSERT_TRUE_MESSAGE(tinylink::is_reserved_message_type(wire[i]), context);
            TEST_ASSERT_TRUE_MESSAGE(tinylink::is_valid_message_type(types[i]), context);
            TEST_ASSERT_TRUE_MESSAGE(
                tinylink::message_type_from_wire(wire[i], decoded), context);
            TEST_ASSERT_EQUAL_MESSAGE(
                static_cast<int>(types[i]), static_cast<int>(decoded), context);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                names[i], tinylink::message_type_to_string(types[i]), context);
        }

        tinylink::MessageType unchanged = tinylink::MessageType::Ack;
        TEST_ASSERT_FALSE(tinylink::is_reserved_message_type('X'));
        TEST_ASSERT_FALSE(tinylink::message_type_from_wire('X', unchanged));
        TEST_ASSERT_FALSE(tinylink::is_valid_message_type(
            static_cast<tinylink::MessageType>('X')));
        TEST_ASSERT_EQUAL(
            static_cast<int>(tinylink::MessageType::Ack),
            static_cast<int>(unchanged));
        TEST_ASSERT_EQUAL_STRING(
            "Unknown",
            tinylink::message_type_to_string(static_cast<tinylink::MessageType>('X')));
    }

    void testStatePredicatesAndDiagnosticsCoverDeclaredAndInvalidValues()
    {
        const tinylink::ConnectionState states[] = {
            tinylink::ConnectionState::Inactive,
            tinylink::ConnectionState::Connecting,
            tinylink::ConnectionState::Connected,
            tinylink::ConnectionState::Incompatible,
            tinylink::ConnectionState::Failed
        };
        const char* names[] = {
            "Inactive", "Connecting", "Connected", "Incompatible", "Failed"
        };

        for (size_t i = 0; i < 5; ++i) {
            const char* context = tinylink_test::diag(
                "connection state=%s index=%u", names[i], (unsigned)i);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                names[i], tinylink::connection_state_to_string(states[i]), context);
            TEST_ASSERT_EQUAL_MESSAGE(i == 2, tinylink::is_connected(states[i]), context);
            TEST_ASSERT_FALSE_MESSAGE(
                tinylink::can_send(states[i], tinylink::TransmitState::AwaitingAck),
                context);
            TEST_ASSERT_EQUAL_MESSAGE(i == 2, tinylink::can_send(
                states[i], tinylink::TransmitState::Ready), context);
        }
        TEST_ASSERT_EQUAL_STRING(
            "Invalid",
            tinylink::connection_state_to_string(
                static_cast<tinylink::ConnectionState>(0xFFu)));

        TEST_ASSERT_FALSE(tinylink::is_awaiting_ack(tinylink::TransmitState::Ready));
        TEST_ASSERT_TRUE(tinylink::is_awaiting_ack(tinylink::TransmitState::AwaitingAck));
        TEST_ASSERT_FALSE(tinylink::is_awaiting_ack(
            static_cast<tinylink::TransmitState>(0xFFu)));
        TEST_ASSERT_FALSE(tinylink::can_send(
            tinylink::ConnectionState::Connected,
            static_cast<tinylink::TransmitState>(0xFFu)));
    }

    void testVersionMacrosKeepLibraryAndWireVersionsIndependent()
    {
        static_assert(TINYLINK_VERSION ==
            TINYLINK_VERSION_ENCODE(TINYLINK_VERSION_MAJOR,
                TINYLINK_VERSION_MINOR, TINYLINK_VERSION_PATCH),
            "release version encoding changed");
        static_assert(TINYLINK_PROTOCOL_VERSION > 0,
            "protocol version must be positive");
        static_assert(TINYLINK_VERSION != TINYLINK_PROTOCOL_VERSION,
            "library release version is independent from wire version");

        TEST_ASSERT_EQUAL_UINT32(0, TINYLINK_VERSION_MAJOR);
        TEST_ASSERT_EQUAL_UINT32(5, TINYLINK_VERSION_MINOR);
        TEST_ASSERT_EQUAL_UINT32(0, TINYLINK_VERSION_PATCH);
        TEST_ASSERT_EQUAL_UINT32(500, TINYLINK_VERSION);
        TEST_ASSERT_EQUAL_UINT32(1, TINYLINK_PROTOCOL_VERSION);
        TEST_ASSERT_EQUAL_STRING("0.5.0", TINYLINK_VERSION_STR);
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testAckResultWireValuesStringsAndInvalidConversions);
    RUN_TEST(testAckMessageCodecRoundTripsAllDeclaredResults);
    RUN_TEST(testAckCodecErrorsPreserveDestinationsAndOutput);
    RUN_TEST(testHandshakeKindsAndWireValuesAreStable);
    RUN_TEST(testHandshakeCodecRoundTripsRequestAndResponse);
    RUN_TEST(testHandshakeCodecRejectsInvalidInputsWithoutMutation);
    RUN_TEST(testLogTextSetterHandlesNullEmptyExactAndTruncatedStrings);
    RUN_TEST(testLogCodecUsesDocumentedLittleEndianWireLayoutAndRoundTrips);
    RUN_TEST(testLogCodecRejectsInvalidCapacityLevelLengthAndTextWithoutMutation);
    RUN_TEST(testLogLevelValuesConversionsThresholdsAndDiagnostics);
    RUN_TEST(testMessageTypeValuesReservedConversionsAndDiagnostics);
    RUN_TEST(testStatePredicatesAndDiagnosticsCoverDeclaredAndInvalidValues);
    RUN_TEST(testVersionMacrosKeepLibraryAndWireVersionsIndependent);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
