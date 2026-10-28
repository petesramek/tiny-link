#ifndef TINYLINK_TEST_FIXTURE_H
#define TINYLINK_TEST_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#include "support/tinylink/TinyLinkTestHelpers.h"

namespace tinylink {

    struct TinyLinkStatsAccess {
        template <typename L>
        static Stats& stats(L& link) { return link._stats; }
    };

} // namespace tinylink

namespace tinylink_test {

    /** @brief Payload type used by most engine tests (4 bytes). */
    struct Payload {
        uint8_t bytes[4];
    };

    /** @brief Payload at the maximum supported size (PACKET_MAX_PAYLOAD_SIZE, 64). */
    struct MaximumPayload {
        uint8_t bytes[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE];
    };

    typedef tinylink::TinyLink<Payload, FakeAdapter> Link;

    /*
     * Callback recorders: onData/onLog/onHandshake/onAck bump a counter and
     * store the last argument in the globals below. They are shared by the
     * engine suite's translation units; setUp() calls resetCallbacks().
     * `ALL` (1000) is a scriptWrites() limit meaning "accept every byte" (it
     * exceeds any frame size).
     */
    extern unsigned int dataCallbackCount;
    extern unsigned int logCallbackCount;
    extern unsigned int handshakeCallbackCount;
    extern unsigned int ackCallbackCount;
    extern Payload lastData;
    extern tinylink::LogMessage lastLog;
    extern tinylink::HandshakeMessage lastHandshake;
    extern uint8_t lastAckSequence;
    extern tinylink::AckResult lastAckResult;
    extern const size_t ALL;

    void resetCallbacks();
    void onData(const Payload& payload);
    void onLog(const tinylink::LogMessage& message);
    void onHandshake(const tinylink::HandshakeMessage& message);
    void onAck(uint8_t sequence, tinylink::AckResult result);
    Payload payloadWith(uint8_t first);
    bool connectPeer(FakeAdapter& adapter, Link& link);
    bool receiveApplication(FakeAdapter& adapter, Link& link, uint8_t sequence);
    uint8_t ackedSequence(const FakeAdapter& adapter);
    void scriptWrites(FakeAdapter& adapter, const size_t* limits, size_t count);
    uint8_t sentSequence(FakeAdapter& adapter, size_t frame);

} // namespace tinylink_test

#endif
