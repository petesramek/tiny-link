#include <string.h>
#include <unity.h>

#include "support/tinylink/TinyLinkTestFixture.h"

namespace tinylink_test {

    unsigned int dataCallbackCount = 0;
    unsigned int logCallbackCount = 0;
    unsigned int handshakeCallbackCount = 0;
    unsigned int ackCallbackCount = 0;
    Payload lastData = {};
    tinylink::LogMessage lastLog = {};
    tinylink::HandshakeMessage lastHandshake = {
        tinylink::HandshakeKind::Request, 0
    };
    uint8_t lastAckSequence = 0;
    tinylink::AckResult lastAckResult = tinylink::AckResult::Unknown;
    const size_t ALL = 1000;

    void resetCallbacks()
    {
        dataCallbackCount = 0;
        logCallbackCount = 0;
        handshakeCallbackCount = 0;
        ackCallbackCount = 0;
        memset(&lastData, 0, sizeof(lastData));
        memset(&lastLog, 0, sizeof(lastLog));
        lastHandshake.kind = tinylink::HandshakeKind::Request;
        lastHandshake.protocolVersion = 0;
        lastAckSequence = 0;
        lastAckResult = tinylink::AckResult::Unknown;
    }

    void onData(const Payload& payload)
    {
        ++dataCallbackCount;
        lastData = payload;
    }

    void onLog(const tinylink::LogMessage& message)
    {
        ++logCallbackCount;
        lastLog = message;
    }

    void onHandshake(const tinylink::HandshakeMessage& message)
    {
        ++handshakeCallbackCount;
        lastHandshake = message;
    }

    void onAck(uint8_t sequence, tinylink::AckResult result)
    {
        ++ackCallbackCount;
        lastAckSequence = sequence;
        lastAckResult = result;
    }

    Payload payloadWith(uint8_t first)
    {
        Payload payload = {{ first, static_cast<uint8_t>(first + 1),
            static_cast<uint8_t>(first + 2), static_cast<uint8_t>(first + 3) }};
        return payload;
    }

    bool connectPeer(FakeAdapter& adapter, Link& link)
    {
        if (!tinylink_test::queueHandshake(
                adapter, tinylink::HandshakeKind::Request)) {
            return false;
        }
        link.update();
        adapter.clearOutput();
        return link.connected();
    }

    bool receiveApplication(FakeAdapter& adapter, Link& link, uint8_t sequence)
    {
        const Payload payload = payloadWith(0x10);
        if (!tinylink_test::queueApplication(
                adapter, 0x20, sequence, payload.bytes, sizeof(payload.bytes))) {
            return false;
        }
        link.update();
        adapter.clearOutput();
        return true;
    }

    uint8_t ackedSequence(const FakeAdapter& adapter)
    {
        uint8_t type = 0;
        uint8_t sequence = 0;
        uint8_t payload[tinylink::ACK_MESSAGE_WIRE_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, 0, type, sequence, payload, sizeof(payload), length));
        TEST_ASSERT_EQUAL_UINT8(
            tinylink::message_type_to_wire(tinylink::MessageType::Ack), type);
        return sequence;
    }

    void scriptWrites(FakeAdapter& adapter, const size_t* limits, size_t count)
    {
        adapter.scriptWrites(limits, count);
    }

    uint8_t sentSequence(FakeAdapter& adapter, size_t frame)
    {
        uint8_t type = 0;
        uint8_t sequence = 0xEE;
        uint8_t payload[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE] = {};
        size_t length = 0;
        TEST_ASSERT_TRUE(tinylink_test::decodeWrittenFrame(
            adapter, frame, type, sequence, payload, sizeof(payload), length));
        return sequence;
    }

} // namespace tinylink_test
