/**
 * @file test_duplex.cpp
 * @brief Full-duplex two-instance communication tests for TinyLink.
 *
 * Uses two cross-wired TinyDuplexTestAdapter instances to simulate a real
 * bidirectional serial wire.  Every test here exercises some aspect of
 * simultaneous or interleaved two-way communication: data frames, log frames,
 * callbacks, polling, ACK handling, stats tracking, burst throughput, and
 * connection recovery.
 *
 * Test helpers local to this file
 * ─────────────────────────────────
 *   resetDuplex()   – wires A↔B, zeroes clocks/buffers, resets both engines
 *   connectBoth()   – calls begin() on both sides and pumps until connected
 *   pumpN(n)        – runs update() on both sides n times
 *   pumpUntilIdle() – runs update() until neither adapter has bytes waiting
 */

#include <unity.h>
#include "globals.h"
#include "adapters/TinyDuplexTestAdapter.h"
#include "internal/protocol/AckMessage.h"
#include "internal/protocol/HandshakeMessage.h"
#include "internal/protocol/LogMessage.h"
#include "internal/codec/CobsCodec.h"
#include "internal/codec/Packet.h"

using namespace tinylink;

// ---------------------------------------------------------------------------
// Shared state for this file
// ---------------------------------------------------------------------------

static TinyDuplexTestAdapter dA, dB;
static TinyLink<TestPayload, TinyDuplexTestAdapter> lA(dA), lB(dB);

static void resetDuplex() {
    dA.connect(dB);
    dB.connect(dA);
    dA.setMillis(0);
    dB.setMillis(0);
    dA.getRawBuffer().clear();
    dB.getRawBuffer().clear();
    lA.reset();
    lB.reset();
}

static void pumpN(int n) {
    for (int i = 0; i < n; ++i) {
        lA.update();
        lB.update();
    }
}

static void pumpUntilIdle(int maxPasses = 40) {
    for (int i = 0; i < maxPasses; ++i) {
        bool hadWork = (dA.available() > 0) || (dB.available() > 0);
        lA.update();
        lB.update();
        if (!hadWork) break;
    }
}

static void connectBoth() {
    lA.begin();
    lB.begin();
    pumpUntilIdle();
}

// ---------------------------------------------------------------------------
// Helper: manually inject a raw ACK frame into an adapter's RX buffer.
// ---------------------------------------------------------------------------

static void injectAck(TinyDuplexTestAdapter& into, uint8_t seq) {
    TinyAck ack;
    ack.seq    = seq;
    ack.result = TinyStatus::STATUS_OK;

    const size_t PLAIN = 3 + sizeof(TinyAck) + 2;
    const size_t ENC   = PLAIN + 4;
    uint8_t pBuf[PLAIN];
    uint8_t rawBuf[ENC];

    size_t plainLen = tinylink::packet::pack(
        message_type_to_wire(MessageType::Ack), 0,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack),
        pBuf, PLAIN);
    size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen, rawBuf, ENC);

    uint8_t zero = 0x00;
    into.inject(&zero, 1);
    into.inject(rawBuf, eLen);
    into.inject(&zero, 1);
}

// ===========================================================================
// 1. Basic bidirectional exchange
// ===========================================================================

/**
 * @test After handshake A sends to B (polling) and B sends to A (polling),
 *       both packets are correctly received in sequence.
 */
void test_duplex_sequential_bidirectional(void) {
    resetDuplex();
    connectBoth();
    TEST_ASSERT_TRUE(lA.connected());
    TEST_ASSERT_TRUE(lB.connected());

    // A → B
    TestPayload toB = {0xAABB, 1.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), toB);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(lB.available(), "B should receive A's packet");
    TEST_ASSERT_EQUAL_UINT32(0xAABB, lB.peek().uptime);
    lB.flush();

    // B → A
    TestPayload toA = {0xCCDD, 2.0f};
    lB.sendData(static_cast<uint8_t>(MessageType::Data), toA);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(lA.available(), "A should receive B's packet");
    TEST_ASSERT_EQUAL_UINT32(0xCCDD, lA.peek().uptime);
    lA.flush();
}

// ===========================================================================
// 2. Simultaneous bidirectional sends
// ===========================================================================

/**
 * @test Both A and B send a data frame at the same instant (both enter
 *       AWAITING_ACK simultaneously).  After pumping, each side has received
 *       the other's frame via callbacks.
 */
void test_duplex_simultaneous_sends_callbacks(void) {
    resetDuplex();
    connectBoth();

    static bool aReceived, bReceived;
    static uint32_t aVal, bVal;
    aReceived = bReceived = false;
    aVal = bVal = 0;

    lA.onDataReceived([](const TestPayload& p) { aReceived = true; aVal = p.uptime; });
    lB.onDataReceived([](const TestPayload& p) { bReceived = true; bVal = p.uptime; });

    TestPayload pA = {0xA1A1, 1.1f};
    TestPayload pB = {0xB2B2, 2.2f};

    lA.sendData(static_cast<uint8_t>(MessageType::Data), pA); // A→B, A enters AWAITING_ACK
    lB.sendData(static_cast<uint8_t>(MessageType::Data), pB); // B→A, B enters AWAITING_ACK

    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lB.state());

    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(aReceived, "A's callback must fire when B sends");
    TEST_ASSERT_TRUE_MESSAGE(bReceived, "B's callback must fire when A sends");
    TEST_ASSERT_EQUAL_UINT32(0xB2B2, aVal);
    TEST_ASSERT_EQUAL_UINT32(0xA1A1, bVal);
}

// ===========================================================================
// 3. Simultaneous sends — polling mode on both sides
// ===========================================================================

/**
 * @test Both sides use polling (no callbacks).  Both send simultaneously,
 *       both must have available() == true after pumping.
 */
void test_duplex_simultaneous_sends_polling(void) {
    resetDuplex();
    connectBoth();

    TestPayload pA = {0x1111, 1.0f};
    TestPayload pB = {0x2222, 2.0f};

    lA.sendData(static_cast<uint8_t>(MessageType::Data), pA);
    lB.sendData(static_cast<uint8_t>(MessageType::Data), pB);

    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(lA.available(), "A should have B's data waiting");
    TEST_ASSERT_EQUAL_UINT32(0x2222, lA.peek().uptime);

    TEST_ASSERT_TRUE_MESSAGE(lB.available(), "B should have A's data waiting");
    TEST_ASSERT_EQUAL_UINT32(0x1111, lB.peek().uptime);
}

// ===========================================================================
// 4. Mixed polling (A) + callback (B)
// ===========================================================================

/**
 * @test A uses polling, B uses a data callback.  Each side sends one frame;
 *       both receptions are correctly dispatched.
 */
void test_duplex_mixed_polling_and_callback(void) {
    resetDuplex();
    connectBoth();

    static bool bFired;
    static uint32_t bVal;
    bFired = false; bVal = 0;
    lB.onDataReceived([](const TestPayload& p) { bFired = true; bVal = p.uptime; });

    TestPayload pA = {0xFACE, 3.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), pA);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(bFired, "B callback must fire for A's frame");
    TEST_ASSERT_EQUAL_UINT32(0xFACE, bVal);

    TestPayload pB = {0xBEEF, 4.0f};
    lB.sendData(static_cast<uint8_t>(MessageType::Data), pB);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(lA.available(), "A polling must see B's frame");
    TEST_ASSERT_EQUAL_UINT32(0xBEEF, lA.peek().uptime);
}

// ===========================================================================
// 5. Multi-round exchange
// ===========================================================================

/**
 * @test Five complete A→B / B→A round-trips; every message is delivered
 *       intact and stats are accumulated on both sides.
 */
void test_duplex_multi_round_exchange(void) {
    resetDuplex();
    connectBoth();

    for (int round = 1; round <= 5; ++round) {
        // A → B
        TestPayload pA = {static_cast<uint32_t>(round * 10), static_cast<float>(round)};
        lA.sendData(static_cast<uint8_t>(MessageType::Data), pA);
        pumpUntilIdle();

        TEST_ASSERT_TRUE(lB.available());
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(round * 10), lB.peek().uptime);
        lB.flush();

        // B → A
        TestPayload pB = {static_cast<uint32_t>(round * 100), static_cast<float>(round * 10)};
        lB.sendData(static_cast<uint8_t>(MessageType::Data), pB);
        pumpUntilIdle();

        TEST_ASSERT_TRUE(lA.available());
        TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(round * 100), lA.peek().uptime);
        lA.flush();
    }

    TEST_ASSERT_EQUAL_UINT32(5, lA.getStats().packets); // 5 received from B
    TEST_ASSERT_EQUAL_UINT32(5, lB.getStats().packets); // 5 received from A
}

// ===========================================================================
// 6. Burst from one side
// ===========================================================================

/**
 * @test B sends 10 data frames back-to-back to A; A uses a callback counter
 *       to verify all 10 are received.
 */
void test_duplex_burst_from_b_to_a(void) {
    resetDuplex();
    connectBoth();

    static int aCount;
    aCount = 0;
    lA.onDataReceived([](const TestPayload&) { aCount++; });

    for (int i = 0; i < 10; ++i) {
        TestPayload p = {static_cast<uint32_t>(i), static_cast<float>(i)};
        lB.sendData(static_cast<uint8_t>(MessageType::Data), p);
    }

    // Pump enough times for all 10 frames to arrive and be dispatched.
    for (int i = 0; i < 60; ++i) {
        lA.update();
        lB.update();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(10, aCount, "All 10 frames from B must reach A");
}

// ===========================================================================
// 7. Interleaved burst — both sides send N messages
// ===========================================================================

/**
 * @test A and B each queue 5 data frames; after pumping every message from
 *       both sides is delivered (verified via callback counters).
 */
void test_duplex_interleaved_burst(void) {
    resetDuplex();
    connectBoth();

    static int aCount, bCount;
    aCount = bCount = 0;
    lA.onDataReceived([](const TestPayload&) { aCount++; });
    lB.onDataReceived([](const TestPayload&) { bCount++; });

    for (int i = 0; i < 5; ++i) {
        TestPayload p = {static_cast<uint32_t>(i), static_cast<float>(i)};
        lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
        lB.sendData(static_cast<uint8_t>(MessageType::Data), p);
        pumpN(4);
    }

    // Drain any remaining bytes.
    pumpUntilIdle();

    TEST_ASSERT_EQUAL_INT_MESSAGE(5, aCount, "A must receive all 5 frames from B");
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, bCount, "B must receive all 5 frames from A");
}

// ===========================================================================
// 8. Log frames in both directions
// ===========================================================================

/**
 * @test A sends a log to B; B sends a log to A.  Both onLogReceived
 *       callbacks fire with the correct code values.
 */
void test_duplex_log_frames_both_directions(void) {
    resetDuplex();
    connectBoth();

    static bool aLogFired, bLogFired;
    static uint16_t aCode, bCode;
    aLogFired = bLogFired = false;
    aCode = bCode = 0;

    lA.onLogReceived([](const LogMessage& m) { aLogFired = true; aCode = m.code; });
    lB.onLogReceived([](const LogMessage& m) { bLogFired = true; bCode = m.code; });

    lA.sendLog(LogLevel::INFO,  0x1111, "from-A");
    lB.sendLog(LogLevel::WARN,  0x2222, "from-B");

    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(bLogFired, "B must receive A's log frame");
    TEST_ASSERT_EQUAL_UINT16(0x1111, bCode);
    TEST_ASSERT_TRUE_MESSAGE(aLogFired, "A must receive B's log frame");
    TEST_ASSERT_EQUAL_UINT16(0x2222, aCode);
}

// ===========================================================================
// 9. Mixed Data + Log crossing in both directions
// ===========================================================================

/**
 * @test A sends a Data frame to B while B sends a Log frame to A.
 *       The data callback fires on B, the log callback fires on A;
 *       neither is cross-dispatched.
 */
void test_duplex_mixed_data_and_log(void) {
    resetDuplex();
    connectBoth();

    static bool bDataFired, aLogFired, aDataFired, bLogFired;
    bDataFired = aLogFired = aDataFired = bLogFired = false;

    lA.onDataReceived([](const TestPayload&) { aDataFired = true; });
    lA.onLogReceived( [](const LogMessage&)  { aLogFired  = true; });
    lB.onDataReceived([](const TestPayload&) { bDataFired = true; });
    lB.onLogReceived( [](const LogMessage&)  { bLogFired  = true; });

    TestPayload p = {0xDEAD, 9.9f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    lB.sendLog(LogLevel::ERROR, 0xABCD, "log-from-B");

    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(bDataFired, "B must receive A's data frame");
    TEST_ASSERT_TRUE_MESSAGE(aLogFired,  "A must receive B's log frame");
    TEST_ASSERT_FALSE_MESSAGE(aDataFired, "A must NOT receive data — it only sent");
    TEST_ASSERT_FALSE_MESSAGE(bLogFired,  "B must NOT receive a log — it only sent");
}

// ===========================================================================
// 10. Log is not dispatched as data (duplex context)
// ===========================================================================

/**
 * @test A's data callback must not fire for B's log frame, and
 *       B's log callback must not fire for A's data frame, even when
 *       both callbacks are registered on both sides.
 */
void test_duplex_no_cross_dispatch(void) {
    resetDuplex();
    connectBoth();

    static int aDataCount, aLogCount, bDataCount, bLogCount;
    aDataCount = aLogCount = bDataCount = bLogCount = 0;

    lA.onDataReceived([](const TestPayload&) { aDataCount++; });
    lA.onLogReceived( [](const LogMessage&)  { aLogCount++;  });
    lB.onDataReceived([](const TestPayload&) { bDataCount++; });
    lB.onLogReceived( [](const LogMessage&)  { bLogCount++;  });

    TestPayload p = {0x1234, 5.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);   // B should see data
    lA.sendLog(LogLevel::DEBUG, 0x5678, "debug-A");             // B should see log

    lB.sendData(static_cast<uint8_t>(MessageType::Data), p);   // A should see data
    lB.sendLog(LogLevel::TRACE, 0x9ABC, "trace-B");             // A should see log

    pumpUntilIdle();

    TEST_ASSERT_EQUAL_INT(1, aDataCount);
    TEST_ASSERT_EQUAL_INT(1, aLogCount);
    TEST_ASSERT_EQUAL_INT(1, bDataCount);
    TEST_ASSERT_EQUAL_INT(1, bLogCount);
}

// ===========================================================================
// 11. Stats are tracked independently per instance
// ===========================================================================

/**
 * @test After a duplex exchange, each side's 'packets' counter reflects only
 *       what that side received, not what it sent.
 */
void test_duplex_stats_independent(void) {
    resetDuplex();
    connectBoth();

    // A sends 3 packets to B; B sends 2 packets to A.
    TestPayload p = {1, 1.0f};
    for (int i = 0; i < 3; ++i) {
        lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    }
    for (int i = 0; i < 2; ++i) {
        lB.sendData(static_cast<uint8_t>(MessageType::Data), p);
    }

    // Pump with flush to allow all data frames to be processed
    for (int pass = 0; pass < 80; ++pass) {
        if (lA.available()) lA.flush();
        if (lB.available()) lB.flush();
        lA.update();
        lB.update();
    }

    TEST_ASSERT_EQUAL_UINT32(2, lA.getStats().packets); // A received 2 from B
    TEST_ASSERT_EQUAL_UINT32(3, lB.getStats().packets); // B received 3 from A
    TEST_ASSERT_EQUAL_UINT32(0, lA.getStats().crc);
    TEST_ASSERT_EQUAL_UINT32(0, lB.getStats().crc);
}

// ===========================================================================
// 12. Sequence counters are independent per instance
// ===========================================================================

/**
 * @test A and B maintain separate, independently incrementing sequence
 *       counters.  Sending from one side must not affect the other's counter.
 */
void test_duplex_seq_counters_independent(void) {
    resetDuplex();
    connectBoth();

    // Prime A's counter by sending 3 frames (no loopback, frames go to B).
    TestPayload p = {0, 0.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    uint8_t seqA = lA.seq(); // seq() returns last-used seq: 3 sends → seqs 0,1,2 → last = 2

    // B has only sent 0 frames — its seq should still be 0.
    // Trigger B's counter with one send.
    lB.sendData(static_cast<uint8_t>(MessageType::Data), p);
    uint8_t seqB = lB.seq(); // seq() returns last-used seq: 1 send → seq 0 → last = 0

    TEST_ASSERT_EQUAL_UINT8(2, seqA);
    TEST_ASSERT_EQUAL_UINT8(0, seqB);
    TEST_ASSERT_NOT_EQUAL(seqA, seqB);
}

// ===========================================================================
// 13. Both sides in AWAITING_ACK simultaneously — data still flows
// ===========================================================================

/**
 * @test When both A and B are in AWAITING_ACK at the same time,
 *       each side can still receive the other's data frame via callback.
 *       Neither side generates an automatic ACK; both remain AWAITING_ACK.
 */
void test_duplex_both_awaiting_ack_can_still_receive(void) {
    resetDuplex();
    connectBoth();

    static bool aReceived, bReceived;
    aReceived = bReceived = false;

    lA.onDataReceived([](const TestPayload& p) { if (p.uptime == 0xB0) aReceived = true; });
    lB.onDataReceived([](const TestPayload& p) { if (p.uptime == 0xA0) bReceived = true; });

    TestPayload pA = {0xA0, 10.0f};
    TestPayload pB = {0xB0, 20.0f};

    lA.sendData(static_cast<uint8_t>(MessageType::Data), pA);
    lB.sendData(static_cast<uint8_t>(MessageType::Data), pB);

    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lB.state());

    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(aReceived, "A must receive B's frame while AWAITING_ACK");
    TEST_ASSERT_TRUE_MESSAGE(bReceived, "B must receive A's frame while AWAITING_ACK");

    // Neither got an ACK → both remain in AWAITING_ACK.
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lB.state());
}

// ===========================================================================
// 14. ACK callbacks on both sides
// ===========================================================================

/**
 * @test A sends to B; an ACK is injected back into A's buffer.
 *       B sends to A; an ACK is injected back into B's buffer.
 *       Both onAckReceived callbacks must fire.
 */
void test_duplex_ack_callbacks_both_sides(void) {
    resetDuplex();
    connectBoth();

    static bool aAckFired, bAckFired;
    aAckFired = bAckFired = false;
    lA.onAckReceived([](const TinyAck&) { aAckFired = true; });
    lB.onAckReceived([](const TinyAck&) { bAckFired = true; });

    // A sends → enters AWAITING_ACK; inject ACK into A's RX buffer.
    TestPayload p = {1, 1.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());

    // Drain A's data frame from B's buffer first so dB is clean.
    lB.update();
    if (lB.available()) lB.flush();

    injectAck(dA, lA.seq());
    lA.update();

    TEST_ASSERT_TRUE_MESSAGE(aAckFired, "A's onAckReceived must fire");
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, lA.state());

    // B sends → enters AWAITING_ACK; drain B's data frame from A, then inject ACK.
    lB.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lB.state());

    // Drain B's data frame from A's buffer so dA is clean.
    lA.update();
    if (lA.available()) lA.flush();

    injectAck(dB, lB.seq());
    lB.update();

    TEST_ASSERT_TRUE_MESSAGE(bAckFired, "B's onAckReceived must fire");
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, lB.state());
}

// ===========================================================================
// 15. AWAITING_ACK timeout while other side keeps receiving
// ===========================================================================

/**
 * @test A sends to B and enters AWAITING_ACK.  No ACK arrives for A.
 *       After the timeout, A returns to WAIT_FOR_SYNC.
 *       Meanwhile B can still receive from A (B's state is unaffected).
 */
void test_duplex_awaiting_ack_timeout_does_not_block_b(void) {
    resetDuplex();
    connectBoth();

    static bool bReceived;
    bReceived = false;
    lB.onDataReceived([](const TestPayload& p) { if (p.uptime == 99) bReceived = true; });

    // A sends → AWAITING_ACK; B's RX buffer gets the data frame.
    TestPayload p = {99, 0.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());

    // Clear A's RX so no ACK ever arrives.
    dA.getRawBuffer().clear();

    // Let B process A's data frame.
    pumpUntilIdle();
    TEST_ASSERT_TRUE_MESSAGE(bReceived, "B must receive A's frame before timeout");

    // Advance time past the default 250 ms ACK timeout for A.
    dA.advanceMillis(300);
    lA.update();

    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, lA.state());
    TEST_ASSERT_EQUAL_UINT32(1, lA.getStats().timeout);
}

// ===========================================================================
// 16. Reconnect — one side resets and calls begin() again
// ===========================================================================

/**
 * @test Both sides are connected and exchange one frame.  Then A resets
 *       and calls begin() to reconnect.  B responds to A's new handshake
 *       automatically.  After reconnection both sides exchange data again.
 */
void test_duplex_reconnect_after_reset(void) {
    resetDuplex();
    connectBoth();

    // First exchange: A → B.
    static bool bFirst, bSecond;
    bFirst = bSecond = false;

    lB.onDataReceived([](const TestPayload& p) {
        if (p.uptime == 0x11) bFirst  = true;
        if (p.uptime == 0x22) bSecond = true;
    });

    TestPayload p1 = {0x11, 1.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p1);
    pumpUntilIdle();
    TEST_ASSERT_TRUE_MESSAGE(bFirst, "B must receive A's first packet");

    // A disconnects and reconnects.
    lA.reset();
    lA.begin();
    pumpUntilIdle(); // A's new HS(v=0) → B replies HS(v=1) → A connected

    TEST_ASSERT_TRUE_MESSAGE(lA.connected(), "A must reconnect after reset+begin");

    // Second exchange: A → B.
    TestPayload p2 = {0x22, 2.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p2);
    pumpUntilIdle();
    TEST_ASSERT_TRUE_MESSAGE(bSecond, "B must receive A's second packet after reconnect");
}

// ===========================================================================
// 17. One side uses begin(), the other is in basic mode
// ===========================================================================

/**
 * @test A uses begin() (handshake mode); B stays in basic mode.
 *       After A connects, data flows in both directions.
 */
void test_duplex_one_sided_handshake_both_directions(void) {
    resetDuplex();

    // Only A initiates a handshake.
    lA.begin();
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(lA.connected(), "A must connect after one-sided begin()");
    TEST_ASSERT_TRUE_MESSAGE(lB.connected(), "B must be connected in basic mode");

    // A → B
    static bool bGotA, aGotB;
    bGotA = aGotB = false;
    lA.onDataReceived([](const TestPayload& p) { if (p.uptime == 0xBB) aGotB = true; });
    lB.onDataReceived([](const TestPayload& p) { if (p.uptime == 0xAA) bGotA = true; });

    TestPayload pA = {0xAA, 1.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), pA);
    pumpUntilIdle();
    TEST_ASSERT_TRUE_MESSAGE(bGotA, "B (basic mode) must receive A's frame");

    // B → A
    TestPayload pB = {0xBB, 2.0f};
    lB.sendData(static_cast<uint8_t>(MessageType::Data), pB);
    pumpUntilIdle();
    TEST_ASSERT_TRUE_MESSAGE(aGotB, "A (handshake mode) must receive B's frame");
}

// ===========================================================================
// 18. Noise on one side does not disrupt the other side's reception
// ===========================================================================

/**
 * @test Junk bytes injected directly into A's RX buffer do not prevent
 *       B from correctly receiving a clean data frame from A.
 */
void test_duplex_noise_on_a_does_not_corrupt_b(void) {
    resetDuplex();
    connectBoth();

    static bool bReceived;
    bReceived = false;
    lB.onDataReceived([](const TestPayload& p) { if (p.uptime == 0xC0) bReceived = true; });

    // Inject junk into A's own RX (simulates noise on A's line from an external source).
    uint8_t junk[] = {0xFF, 0xAA, 0x12};
    dA.inject(junk, sizeof(junk));

    // A still sends a clean frame to B.
    TestPayload p = {0xC0, 7.0f};
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(bReceived, "B must receive A's frame despite noise in A's RX");
    // A itself may have a CRC/timeout error from the junk — that's expected.
    TEST_ASSERT_EQUAL_UINT32(0, lB.getStats().crc);
}

// ===========================================================================
// 19. Long sequence wrap-around in duplex mode
// ===========================================================================

/**
 * @test 256 frames from A to B; the sequence counter wraps correctly
 *       and all frames pass CRC, so B accumulates exactly 256 'packets'.
 */
void test_duplex_sequence_wrap_in_full_duplex(void) {
    resetDuplex();
    connectBoth();

    static int bCount;
    bCount = 0;
    lB.onDataReceived([](const TestPayload&) { bCount++; });

    TestPayload p = {42, 3.14f};
    for (int i = 0; i < 256; ++i) {
        lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
        pumpN(4);
    }
    pumpUntilIdle();

    TEST_ASSERT_EQUAL_INT_MESSAGE(256, bCount, "All 256 frames must arrive at B");
    TEST_ASSERT_EQUAL_UINT32(0, lB.getStats().crc);
    TEST_ASSERT_EQUAL_UINT8(255, lA.seq()); // seq() returns last-used seq: 256 sends → seqs 0..255 → last = 255
}

// ===========================================================================
// 20. Payload data integrity preserved across full-duplex exchange
// ===========================================================================

/**
 * @test A sends a frame with specific float/uint values; B sends back a
 *       frame with different values.  The payloads do not interfere with
 *       each other and are bit-perfectly received on both ends.
 */
void test_duplex_payload_integrity(void) {
    resetDuplex();
    connectBoth();

    static TestPayload aReceived, bReceived;
    static bool aGot, bGot;
    aGot = bGot = false;

    lA.onDataReceived([](const TestPayload& p) { aReceived = p; aGot = true; });
    lB.onDataReceived([](const TestPayload& p) { bReceived = p; bGot = true; });

    TestPayload fromA = {0x11223344, -1.5f};
    TestPayload fromB = {0xAABBCCDD, 99.99f};

    lA.sendData(static_cast<uint8_t>(MessageType::Data), fromA);
    lB.sendData(static_cast<uint8_t>(MessageType::Data), fromB);

    pumpUntilIdle();

    TEST_ASSERT_TRUE(aGot);
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, aReceived.uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 99.99f, aReceived.value);

    TEST_ASSERT_TRUE(bGot);
    TEST_ASSERT_EQUAL_UINT32(0x11223344, bReceived.uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.5f, bReceived.value);
}

// ===========================================================================
// Registration
// ===========================================================================

void register_duplex_tests(void) {
    // Bidirectional data exchange
    RUN_TEST(test_duplex_sequential_bidirectional);
    RUN_TEST(test_duplex_simultaneous_sends_callbacks);
    RUN_TEST(test_duplex_simultaneous_sends_polling);
    RUN_TEST(test_duplex_mixed_polling_and_callback);
    RUN_TEST(test_duplex_payload_integrity);

    // Multi-packet exchanges
    RUN_TEST(test_duplex_multi_round_exchange);
    RUN_TEST(test_duplex_burst_from_b_to_a);
    RUN_TEST(test_duplex_interleaved_burst);

    // Protocol frame types in duplex
    RUN_TEST(test_duplex_log_frames_both_directions);
    RUN_TEST(test_duplex_mixed_data_and_log);
    RUN_TEST(test_duplex_no_cross_dispatch);

    // State machine and ACK handling
    RUN_TEST(test_duplex_both_awaiting_ack_can_still_receive);
    RUN_TEST(test_duplex_ack_callbacks_both_sides);
    RUN_TEST(test_duplex_awaiting_ack_timeout_does_not_block_b);

    // Stats and sequence counters
    RUN_TEST(test_duplex_stats_independent);
    RUN_TEST(test_duplex_seq_counters_independent);

    // Connection lifecycle
    RUN_TEST(test_duplex_reconnect_after_reset);
    RUN_TEST(test_duplex_one_sided_handshake_both_directions);

    // Resilience
    RUN_TEST(test_duplex_noise_on_a_does_not_corrupt_b);

    // Sequence wrap in duplex
    RUN_TEST(test_duplex_sequence_wrap_in_full_duplex);
}
