#include <unity.h>
#include <string.h>
#include "TinyLink.h"
#include "adapters/TinyTestAdapter.h"
#include "globals.h"

using namespace tinylink;

class WriteResultAdapter : public TinyTestAdapter {
    size_t _acceptedOnFailure;
    bool _failNext;

public:
    WriteResultAdapter() : _acceptedOnFailure(0), _failNext(false) {}

    void failNextWrite(size_t accepted) {
        _acceptedOnFailure = accepted;
        _failNext = true;
    }

    TinyWriteResult write(uint8_t c) override {
        return write(&c, 1);
    }

    TinyWriteResult write(const uint8_t* bytes, size_t len) override {
        if (len == 0) return tiny_write_result(0, 0, true);
        if (_failNext) {
            _failNext = false;
            size_t accepted = _acceptedOnFailure == static_cast<size_t>(-1)
                ? len - 1
                : (_acceptedOnFailure < len ? _acceptedOnFailure : len);
            if (accepted > 0) inject(bytes, accepted);
            return tiny_write_result(len, accepted, false);
        }
        inject(bytes, len);
        return tiny_write_result(len, len, true);
    }
};

static WriteResultAdapter writeAdapter;
static TinyLink<TestPayload, WriteResultAdapter> writeLink(writeAdapter);

static void resetWriteTest() {
    writeLink.reset();
    writeAdapter.setMillis(0);
    writeAdapter.getRawBuffer().clear();
}

static void injectHandshake(uint8_t version) {
    HandshakeMessage request;
    request.version = version;
    uint8_t plain[3 + sizeof(request) + 2];
    uint8_t encoded[sizeof(plain) + 4];
    size_t plainLen = packet::pack(
        message_type_to_wire(MessageType::Handshake), 0,
        reinterpret_cast<const uint8_t*>(&request), sizeof(request),
        plain, sizeof(plain));
    size_t encodedLen = codec::cobs_encode(
        plain, plainLen, encoded, sizeof(encoded));
    uint8_t frame[sizeof(encoded) + 2];
    frame[0] = 0;
    memcpy(frame + 1, encoded, encodedLen);
    frame[encodedLen + 1] = 0;
    writeAdapter.inject(frame, encodedLen + 2);
}

void test_send_data_success_reports_complete_write(void) {
    resetWriteTest();
    TestPayload payload = {17, 1.7f};

    TEST_ASSERT_TRUE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL_UINT8(0, writeLink.seq());
    TEST_ASSERT_GREATER_THAN(2, writeAdapter.getRawBuffer().size());
}

void test_send_data_failed_write_does_not_consume_sequence(void) {
    resetWriteTest();
    TestPayload payload = {18, 1.8f};
    writeAdapter.failNextWrite(0);

    TEST_ASSERT_FALSE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, writeLink.state());

    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL_UINT8(0, writeLink.seq());
}

void test_send_data_partial_write_does_not_enter_awaiting_ack_or_consume_sequence(void) {
    resetWriteTest();
    TEST_ASSERT_TRUE(writeLink.begin());
    writeAdapter.getRawBuffer().clear();
    injectHandshake(1);
    writeLink.update();
    TEST_ASSERT_TRUE(writeLink.connected());
    TestPayload payload = {19, 1.9f};
    writeAdapter.failNextWrite(3);

    TEST_ASSERT_FALSE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, writeLink.state());
    TEST_ASSERT_EQUAL_INT(3, writeAdapter.getRawBuffer().size());

    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL_UINT8(0, writeLink.seq());
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, writeLink.state());
}

void test_send_data_missing_final_delimiter_is_incomplete(void) {
    resetWriteTest();
    TestPayload payload = {20, 2.0f};
    writeAdapter.failNextWrite(static_cast<size_t>(-1));

    TEST_ASSERT_FALSE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, writeLink.state());

    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), payload));
    TEST_ASSERT_EQUAL_UINT8(0, writeLink.seq());
}

void test_send_ack_propagates_write_failure(void) {
    resetWriteTest();
    writeAdapter.failNextWrite(0);
    TEST_ASSERT_FALSE(writeLink.sendAck());
}

void test_send_log_failure_does_not_consume_sequence(void) {
    resetWriteTest();
    writeAdapter.failNextWrite(0);

    TEST_ASSERT_FALSE(writeLink.sendLog(LogLevel::INFO, 42, "failed"));
    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.sendLog(LogLevel::INFO, 43, "success"));
    TEST_ASSERT_EQUAL_UINT8(0, writeLink.seq());
}

void test_begin_propagates_handshake_write_failure(void) {
    resetWriteTest();
    writeAdapter.failNextWrite(0);

    TEST_ASSERT_FALSE(writeLink.begin());
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, writeLink.state());
    TEST_ASSERT_FALSE(writeLink.connected());

    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.begin());
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, writeLink.state());
}

void test_handshake_request_response_failure_keeps_link_connecting(void) {
    resetWriteTest();
    TEST_ASSERT_TRUE(writeLink.begin());
    writeAdapter.getRawBuffer().clear();
    writeAdapter.failNextWrite(0);

    injectHandshake(0);

    writeLink.update();
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, writeLink.state());
    TEST_ASSERT_FALSE(writeLink.connected());
}

void register_write_propagation_tests(void) {
    RUN_TEST(test_send_data_success_reports_complete_write);
    RUN_TEST(test_send_data_failed_write_does_not_consume_sequence);
    RUN_TEST(test_send_data_partial_write_does_not_enter_awaiting_ack_or_consume_sequence);
    RUN_TEST(test_send_data_missing_final_delimiter_is_incomplete);
    RUN_TEST(test_send_ack_propagates_write_failure);
    RUN_TEST(test_send_log_failure_does_not_consume_sequence);
    RUN_TEST(test_begin_propagates_handshake_write_failure);
    RUN_TEST(test_handshake_request_response_failure_keeps_link_connecting);
}
