#include <stdint.h>
#include <string.h>

#include <unity.h>

#include "protocol/Stats.h"
#include "protocol/TelemetryEvent.h"

namespace {

    struct EventCounter {
        tinylink::TelemetryEvent event;
        const char* name;
        tinylink::Stats::CountType tinylink::Stats::*counter;
    };

    const EventCounter eventCounters[] = {
        { tinylink::TelemetryEvent::PacketReceived, "PacketReceived",
            &tinylink::Stats::packetsReceived },
        { tinylink::TelemetryEvent::CobsDecodeFailed, "CobsDecodeFailed",
            &tinylink::Stats::cobsDecodeFailures },
        { tinylink::TelemetryEvent::ChecksumMismatch, "ChecksumMismatch",
            &tinylink::Stats::checksumMismatches },
        { tinylink::TelemetryEvent::PacketLengthMismatch, "PacketLengthMismatch",
            &tinylink::Stats::packetLengthMismatches },
        { tinylink::TelemetryEvent::PayloadSizeMismatch, "PayloadSizeMismatch",
            &tinylink::Stats::payloadSizeMismatches },
        { tinylink::TelemetryEvent::PacketEncodeFailed, "PacketEncodeFailed",
            &tinylink::Stats::packetEncodeFailures },
        { tinylink::TelemetryEvent::CobsEncodeFailed, "CobsEncodeFailed",
            &tinylink::Stats::cobsEncodeFailures },
        { tinylink::TelemetryEvent::InterByteTimeout, "InterByteTimeout",
            &tinylink::Stats::interByteTimeouts },
        { tinylink::TelemetryEvent::AckTimeout, "AckTimeout",
            &tinylink::Stats::ackTimeouts },
        { tinylink::TelemetryEvent::ReceiveOverflow, "ReceiveOverflow",
            &tinylink::Stats::receiveOverflows },
        { tinylink::TelemetryEvent::UnexpectedMessageType, "UnexpectedMessageType",
            &tinylink::Stats::unexpectedMessageTypes },
        { tinylink::TelemetryEvent::UnexpectedSequence, "UnexpectedSequence",
            &tinylink::Stats::unexpectedSequences },
        { tinylink::TelemetryEvent::InvalidAckResult, "InvalidAckResult",
            &tinylink::Stats::invalidAckResults },
        { tinylink::TelemetryEvent::InvalidHandshake, "InvalidHandshake",
            &tinylink::Stats::invalidHandshakes }
    };

    void testEveryTelemetryEventMapsToItsOwnCounterAndName()
    {
        tinylink::Stats stats;
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT8(i, static_cast<uint8_t>(eventCounters[i].event));
            TEST_ASSERT_EQUAL_STRING(
                eventCounters[i].name,
                tinylink::telemetry_event_to_string(eventCounters[i].event));
            TEST_ASSERT_TRUE(stats.increment(eventCounters[i].event));
        }

        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT32(1, stats.countFor(eventCounters[i].event));
            TEST_ASSERT_EQUAL_UINT32(1, stats.*(eventCounters[i].counter));
        }
    }

    void testUndeclaredTelemetryEventIsRejectedAndDiagnosticIsInvalid()
    {
        tinylink::Stats stats;
        const tinylink::TelemetryEvent invalid =
            static_cast<tinylink::TelemetryEvent>(0xFFu);
        TEST_ASSERT_FALSE(stats.increment(invalid));
        TEST_ASSERT_EQUAL_UINT32(0, stats.countFor(invalid));
        TEST_ASSERT_EQUAL_STRING("Invalid", tinylink::telemetry_event_to_string(invalid));
        TEST_ASSERT_EQUAL_UINT32(0, stats.packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(0, stats.invalidHandshakes);
    }

    void testStatsInitializeClearAndCopiedSnapshotsAreIndependent()
    {
        tinylink::Stats live;
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT32(0, live.countFor(eventCounters[i].event));
        }

        live.increment(tinylink::TelemetryEvent::PacketReceived);
        live.increment(tinylink::TelemetryEvent::AckTimeout);
        tinylink::Stats snapshot = live;
        snapshot.increment(tinylink::TelemetryEvent::PacketReceived);
        snapshot.clear();

        TEST_ASSERT_EQUAL_UINT32(1, live.packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(1, live.ackTimeouts);
        TEST_ASSERT_EQUAL_UINT32(0, snapshot.packetsReceived);
        TEST_ASSERT_EQUAL_UINT32(0, snapshot.ackTimeouts);
    }

    void testMergeAddsEveryCounterAndSelfMergeDoublesSnapshot()
    {
        tinylink::Stats left;
        tinylink::Stats right;
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            left.*(eventCounters[i].counter) = 3;
            right.*(eventCounters[i].counter) = 5;
        }

        left.merge(right);
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT32(8, left.countFor(eventCounters[i].event));
        }

        left.merge(left);
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT32(16, left.countFor(eventCounters[i].event));
        }
    }

    void testIncrementAndMergeSaturateInsteadOfWrapping()
    {
        tinylink::Stats stats;
        tinylink::Stats addition;
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            stats.*(eventCounters[i].counter) = UINT32_MAX;
            TEST_ASSERT_TRUE(stats.increment(eventCounters[i].event));
            TEST_ASSERT_EQUAL_UINT32(
                UINT32_MAX, stats.*(eventCounters[i].counter));

            stats.*(eventCounters[i].counter) = UINT32_MAX - 1u;
            addition.*(eventCounters[i].counter) = 2;
        }

        stats.merge(addition);
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            TEST_ASSERT_EQUAL_UINT32(
                UINT32_MAX, stats.*(eventCounters[i].counter));
        }
    }

    void testStatsStringFormatsAllCountersInStableOrder()
    {
        tinylink::Stats stats;
        for (size_t i = 0; i < sizeof(eventCounters) / sizeof(eventCounters[0]); ++i) {
            stats.*(eventCounters[i].counter) =
                static_cast<tinylink::Stats::CountType>(i + 1);
        }

        const char expected[] =
            "rx=1 cobs_dec=2 checksum=3 packet_len=4 payload_size=5 "
            "packet_enc=6 cobs_enc=7 inter_byte_tmo=8 ack_tmo=9 overflow=10 "
            "msg_type=11 sequence=12 ack_result=13 handshake=14";
        char output[256] = {};
        const int length = stats.to_string(output, sizeof(output));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(strlen(expected)), length);
        TEST_ASSERT_EQUAL_STRING(expected, output);
    }

    void testStatsStringReportsRequiredLengthAndTerminatesTruncatedBuffers()
    {
        tinylink::Stats stats;
        char complete[256] = {};
        const int required = stats.to_string(complete, sizeof(complete));
        TEST_ASSERT_TRUE(required > 0);

        char shortBuffer[8];
        memset(shortBuffer, 0xA5, sizeof(shortBuffer));
        TEST_ASSERT_EQUAL_INT(required, stats.to_string(shortBuffer, sizeof(shortBuffer)));
        TEST_ASSERT_EQUAL_UINT8(0, shortBuffer[sizeof(shortBuffer) - 1]);
        TEST_ASSERT_EQUAL_UINT8('r', shortBuffer[0]);

        char oneByte[1] = { static_cast<char>(0xA5) };
        TEST_ASSERT_EQUAL_INT(required, stats.to_string(oneByte, sizeof(oneByte)));
        TEST_ASSERT_EQUAL_UINT8(0, oneByte[0]);

        char untouched[] = { 'x' };
        TEST_ASSERT_EQUAL_INT(-1, stats.to_string(nullptr, 1));
        TEST_ASSERT_EQUAL_INT(-1, stats.to_string(untouched, 0));
        TEST_ASSERT_EQUAL_UINT8('x', untouched[0]);
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testEveryTelemetryEventMapsToItsOwnCounterAndName);
    RUN_TEST(testUndeclaredTelemetryEventIsRejectedAndDiagnosticIsInvalid);
    RUN_TEST(testStatsInitializeClearAndCopiedSnapshotsAreIndependent);
    RUN_TEST(testMergeAddsEveryCounterAndSelfMergeDoublesSnapshot);
    RUN_TEST(testIncrementAndMergeSaturateInsteadOfWrapping);
    RUN_TEST(testStatsStringFormatsAllCountersInStableOrder);
    RUN_TEST(testStatsStringReportsRequiredLengthAndTerminatesTruncatedBuffers);
    return UNITY_END();
}

extern "C" void setUp(void)
{
}

extern "C" void tearDown(void)
{
}
