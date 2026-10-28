#ifndef TINYLINK_TEST_TINYLINKTESTHELPERS_H
#define TINYLINK_TEST_TINYLINKTESTHELPERS_H

#include <stddef.h>
#include <stdint.h>

#include "FakeAdapter.h"
#include "TinyLink.h"

namespace tinylink_test {

    inline bool queueFrame(
        FakeAdapter& adapter,
        const uint8_t* plain,
        size_t plainLength)
    {
        uint8_t encoded[tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u] = {};
        const tinylink::codec::CobsResult result =
            tinylink::codec::cobs_encode(
                plain, plainLength, encoded, sizeof(encoded));

        return result.status == tinylink::codec::CobsStatus::Success &&
            adapter.queueBytes(encoded, result.bytesWritten) &&
            adapter.queueByte(0);
    }

    inline bool encodePacket(
        uint8_t type,
        uint8_t sequence,
        const uint8_t* payload,
        size_t payloadLength,
        uint8_t* encoded,
        size_t encodedCapacity,
        size_t& encodedLength)
    {
        uint8_t plain[tinylink::packet::PACKET_MAX_SIZE] = {};
        const tinylink::packet::PacketResult packet =
            tinylink::packet::pack(
                type, sequence, payload, payloadLength,
                plain, sizeof(plain));
        if (packet.status != tinylink::packet::PacketStatus::Success) {
            return false;
        }

        const tinylink::codec::CobsResult cobs =
            tinylink::codec::cobs_encode(
                plain, packet.length, encoded, encodedCapacity);
        if (cobs.status != tinylink::codec::CobsStatus::Success) {
            return false;
        }

        encodedLength = cobs.bytesWritten;
        return true;
    }

    inline bool queuePacket(
        FakeAdapter& adapter,
        uint8_t type,
        uint8_t sequence,
        const uint8_t* payload,
        size_t payloadLength)
    {
        uint8_t encoded[tinylink::packet::PACKET_MAX_SIZE +
            tinylink::packet::PACKET_MAX_SIZE / 254u + 1u] = {};
        size_t encodedLength = 0;
        return encodePacket(
                type, sequence, payload, payloadLength,
                encoded, sizeof(encoded), encodedLength) &&
            adapter.queueBytes(encoded, encodedLength) &&
            adapter.queueByte(0);
    }

    inline bool queueApplication(
        FakeAdapter& adapter,
        uint8_t type,
        uint8_t sequence,
        const uint8_t* payload,
        size_t payloadLength)
    {
        return queuePacket(adapter, type, sequence, payload, payloadLength);
    }

    inline bool queueHandshake(
        FakeAdapter& adapter,
        tinylink::HandshakeKind kind)
    {
        const tinylink::HandshakeMessage message = {
            kind,
            TINYLINK_PROTOCOL_VERSION
        };
        uint8_t payload[tinylink::HANDSHAKE_MESSAGE_WIRE_SIZE] = {};
        if (tinylink::encode_handshake_message(
                message, payload, sizeof(payload)) !=
            tinylink::HandshakeMessageCodecStatus::Success) {
            return false;
        }

        return queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Handshake),
            0,
            payload,
            sizeof(payload));
    }

    inline bool queueAck(
        FakeAdapter& adapter,
        uint8_t sequence,
        tinylink::AckResult result)
    {
        uint8_t payload[tinylink::ACK_MESSAGE_WIRE_SIZE] = {};
        if (tinylink::encode_ack_message(
                result, payload, sizeof(payload)) !=
            tinylink::AckMessageCodecStatus::Success) {
            return false;
        }

        return queuePacket(
            adapter,
            tinylink::message_type_to_wire(tinylink::MessageType::Ack),
            sequence,
            payload,
            sizeof(payload));
    }

    inline size_t writtenFrameCount(const FakeAdapter& adapter)
    {
        size_t frames = 0;
        for (size_t i = 0; i < adapter.outputSize(); ++i) {
            if (adapter.output()[i] == 0) {
                ++frames;
            }
        }
        return frames;
    }

    inline bool decodeWrittenFrame(
        const FakeAdapter& adapter,
        size_t requestedFrame,
        uint8_t& type,
        uint8_t& sequence,
        uint8_t* payload,
        size_t payloadCapacity,
        size_t& payloadLength)
    {
        size_t frameIndex = 0;
        size_t frameStart = 0;

        for (size_t i = 0; i < adapter.outputSize(); ++i) {
            if (adapter.output()[i] != 0) {
                continue;
            }

            if (frameIndex == requestedFrame) {
                uint8_t plain[tinylink::packet::PACKET_MAX_SIZE] = {};
                const tinylink::codec::CobsResult decoded =
                    tinylink::codec::cobs_decode(
                        adapter.output() + frameStart,
                        i - frameStart,
                        plain,
                        sizeof(plain));
                if (decoded.status != tinylink::codec::CobsStatus::Success) {
                    return false;
                }

                const tinylink::packet::PacketResult unpacked =
                    tinylink::packet::unpack(
                        plain,
                        decoded.bytesWritten,
                        &type,
                        &sequence,
                        payload,
                        payloadCapacity,
                        &payloadLength);
                return unpacked.status ==
                    tinylink::packet::PacketStatus::Success;
            }

            ++frameIndex;
            frameStart = i + 1;
        }

        return false;
    }

} // namespace tinylink_test

#endif // TINYLINK_TEST_TINYLINKTESTHELPERS_H
