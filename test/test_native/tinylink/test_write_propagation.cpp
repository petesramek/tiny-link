/**
 * @file test_write_propagation.cpp
 * @brief TinyLink write-result propagation and received-sequence ACK handling.
 */

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "TinyLink.h"
#include "support/stubs/TinyTestAdapter.h"
#include "support/stubs/globals.h"

using namespace tinylink;

/**
 * @class WriteResultAdapter
 * @brief Loopback transport that can fail exactly the next non-empty write.
 *
 * A failure may accept a prefix before reporting failure, or use size_t(-1)
 * to accept every byte except the final one. Subsequent writes succeed.
 */
class WriteResultAdapter : public TinyTestAdapter {
    size_t _acceptedOnFailure;
    bool _failNext; /**< Consumed by the next non-empty write after injection. */

public:
    WriteResultAdapter() : _acceptedOnFailure(0), _failNext(false) {}

    /**
     * @brief Configure one failed write with the requested accepted prefix.
     * @param accepted Bytes to expose before failure; size_t(-1) means len - 1.
     */
    void failNextWrite(size_t accepted) {
        _acceptedOnFailure = accepted;
        _failNext = true;
    }

    /** @brief Clear any armed failure; called by the shared test reset helper. */
    void clearWriteFailure() {
        _acceptedOnFailure = 0;
        _failNext = false;
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

/**
 * @brief Reset the shared link, virtual clock, and captured RX/TX bytes.
 *
 * Write-failure controls are one-shot; this helper also clears any armed fault
 * left behind if an earlier test exits before exercising its failure path.
 */
static void resetWriteTest() {
    writeLink.reset();
    writeAdapter.setMillis(0);
    writeAdapter.getRawBuffer().clear();
    writeAdapter.clearWriteFailure();
}

/** @brief Build and inject a framed handshake request without using link TX. */
static void injectHandshake(uint8_t version) {
    HandshakeMessage request;
    request.version = version;
    uint8_t plain[3 + sizeof(request) + 2]; // Header + payload + FCS.
    uint8_t encoded[sizeof(plain) + 4]; // COBS worst-case overhead for this frame.
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

/** @brief Build and inject one valid data frame with an explicit sequence. */
static void injectData(uint8_t sequence, const TestPayload& payload) {
    uint8_t plain[3 + sizeof(payload) + 2]; // Header + payload + FCS.
    uint8_t encoded[sizeof(plain) + 4]; // COBS worst-case overhead for this frame.
    size_t plainLen = packet::pack(
        message_type_to_wire(MessageType::Data), sequence,
        reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
        plain, sizeof(plain));
    size_t encodedLen = codec::cobs_encode(
        plain, plainLen, encoded, sizeof(encoded));
    uint8_t frame[sizeof(encoded) + 2];
    frame[0] = 0;
    memcpy(frame + 1, encoded, encodedLen);
    frame[encodedLen + 1] = 0;
    writeAdapter.inject(frame, encodedLen + 2);
}

/**
 * @brief Decode the captured buffer as one ACK and return its sequence.
 * @return Sequence number, or -1 when delimiters, type, or payload are invalid.
 */
static int readAckSequence() {
    const std::vector<uint8_t>& frame = writeAdapter.getRawBuffer();
    if (frame.size() < 7 || frame[0] != 0 ||
            frame[frame.size() - 1] != 0) return -1;

    uint8_t plain[3 + sizeof(TinyAck) + 2];
    size_t plainLen = codec::cobs_decode(
        &frame[1], frame.size() - 2, plain, sizeof(plain));
    if (plainLen == 0) return -1;
    uint8_t type = 0, sequence = 0;
    uint8_t payload[sizeof(TinyAck)];
    size_t payloadLen = packet::unpack(
        plain, plainLen, &type, &sequence, payload, sizeof(payload));
    if (type != message_type_to_wire(MessageType::Ack) ||
            payloadLen != sizeof(TinyAck)) return -1;

    TinyAck ack;
    memcpy(&ack, payload, sizeof(ack));
    return ack.seq == sequence ? sequence : -1;
}

/** @brief Callback observation reset explicitly by tests that inspect delivery. */
static bool callbackDelivered;

/** @brief Record callback delivery for sendAck() sequence-preservation tests. */
static void markCallbackDelivered(const TestPayload&) {
    callbackDelivered = true;
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
    TestPayload payload = { 27, 2.7f };
    injectData(20, payload);
    writeLink.update();
    writeAdapter.getRawBuffer().clear();
    writeAdapter.failNextWrite(0);
    TEST_ASSERT_FALSE(writeLink.sendAck());
}

void test_send_ack_requires_received_application_frame(void) {
    resetWriteTest();
    static TinyTestAdapter constructionAdapter;
    static TinyLink<TestPayload, TinyTestAdapter> constructedLink(constructionAdapter);
    TEST_ASSERT_FALSE(constructedLink.sendAck());
    TEST_ASSERT_FALSE(writeLink.sendAck());
    TEST_ASSERT_EQUAL_INT(0, writeAdapter.getRawBuffer().size());
}

void test_received_sequence_survives_callback_delivery(void) {
    resetWriteTest();
    callbackDelivered = false;
    writeLink.onDataReceived(markCallbackDelivered);
    TestPayload payload = { 21, 2.1f };
    injectData(41, payload);
    writeLink.update();

    TEST_ASSERT_TRUE(callbackDelivered);
    TEST_ASSERT_TRUE(writeLink.sendAck());
    TEST_ASSERT_EQUAL_INT(41, readAckSequence());
}

void test_received_sequence_survives_polling_read_and_flush(void) {
    resetWriteTest();
    TestPayload payload = { 22, 2.2f };
    injectData(73, payload);
    writeLink.update();

    TEST_ASSERT_TRUE(writeLink.available());
    TEST_ASSERT_EQUAL_UINT32(payload.uptime, writeLink.peek().uptime);
    writeLink.flush();
    TEST_ASSERT_FALSE(writeLink.available());

    TestPayload outgoing = { 23, 2.3f };
    TEST_ASSERT_TRUE(writeLink.sendData(
        static_cast<uint8_t>(MessageType::Data), outgoing));
    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_TRUE(writeLink.sendAck());
    TEST_ASSERT_EQUAL_INT(73, readAckSequence());
}

void test_reset_invalidates_received_sequence(void) {
    resetWriteTest();
    TestPayload payload = { 24, 2.4f };
    injectData(17, payload);
    writeLink.update();
    TEST_ASSERT_TRUE(writeLink.sendAck());

    writeLink.reset();
    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_FALSE(writeLink.sendAck());
}

void test_begin_invalidates_received_sequence(void) {
    resetWriteTest();
    TestPayload payload = { 25, 2.5f };
    injectData(18, payload);
    writeLink.update();
    TEST_ASSERT_TRUE(writeLink.sendAck());

    TEST_ASSERT_TRUE(writeLink.begin());
    writeAdapter.getRawBuffer().clear();
    TEST_ASSERT_FALSE(writeLink.sendAck());
}

void test_handshake_invalidates_received_sequence(void) {
    resetWriteTest();
    TestPayload payload = { 26, 2.6f };
    injectData(19, payload);
    writeLink.update();
    TEST_ASSERT_TRUE(writeLink.sendAck());

    writeAdapter.getRawBuffer().clear();
    writeLink.flush();
    injectHandshake(1);
    writeLink.update();
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
    RUN_TEST(test_send_ack_requires_received_application_frame);
    RUN_TEST(test_received_sequence_survives_callback_delivery);
    RUN_TEST(test_received_sequence_survives_polling_read_and_flush);
    RUN_TEST(test_reset_invalidates_received_sequence);
    RUN_TEST(test_begin_invalidates_received_sequence);
    RUN_TEST(test_handshake_invalidates_received_sequence);
    RUN_TEST(test_send_log_failure_does_not_consume_sequence);
    RUN_TEST(test_begin_propagates_handshake_write_failure);
    RUN_TEST(test_handshake_request_response_failure_keeps_link_connecting);
}
