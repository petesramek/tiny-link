/**
 * @file test_handshake.cpp
 * @brief Tests for TinyLink::begin(), the two-frame handshake exchange,
 *        and the IN_FRAME / FRAME_COMPLETE / AWAITING_ACK state transitions.
 */

#include <unity.h>
#include "TinyLink.h"
#include "adapters/TinyDuplexTestAdapter.h"
#include "globals.h"  // for TestPayload and the LoopbackAdapter typedef

using namespace tinylink;

// ---------------------------------------------------------------------------
// Helpers shared across tests in this file
// ---------------------------------------------------------------------------

// Two cross-wired duplex adapters and their TinyLink instances.
// Each test that uses duplex communication calls setUpDuplex() first.
static TinyDuplexTestAdapter dAdapterA;
static TinyDuplexTestAdapter dAdapterB;
static TinyLink<TestPayload, TinyDuplexTestAdapter> linkA(dAdapterA);
static TinyLink<TestPayload, TinyDuplexTestAdapter> linkB(dAdapterB);

static void setUpDuplex() {
    dAdapterA.connect(dAdapterB);
    dAdapterB.connect(dAdapterA);
    dAdapterA.setMillis(0);
    dAdapterB.setMillis(0);
    dAdapterA.getRawBuffer().clear();
    dAdapterB.getRawBuffer().clear();
    linkA.reset();
    linkB.reset();
}

// Run update() on both sides until neither has bytes to process.
static void pumpUntilIdle(int maxPasses = 20) {
    for (int i = 0; i < maxPasses; ++i) {
        bool hadWork = (dAdapterA.available() > 0) || (dAdapterB.available() > 0);
        linkA.update();
        linkB.update();
        if (!hadWork) break;
    }
}

// ---------------------------------------------------------------------------
// begin() — basic state and flag checks
// ---------------------------------------------------------------------------

/** @test begin() transitions the link from WAIT_FOR_SYNC to CONNECTING. */
void test_begin_sets_connecting_state(void) {
    // Use the global single link/adapter so this is isolated from duplex state.
    link.reset();
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, link.state());
    link.begin();
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, link.state());
    link.reset();
}

/** @test connected() returns false while in CONNECTING (handshake mode). */
void test_connected_false_while_connecting(void) {
    link.reset();
    link.begin();
    TEST_ASSERT_FALSE(link.connected());
    link.reset();
}

/** @test connected() returns true in basic mode (no begin() called). */
void test_connected_true_basic_mode(void) {
    link.reset();
    TEST_ASSERT_TRUE(link.connected());
}

/** @test sendData() is silently dropped while not yet connected (handshake mode). */
void test_send_blocked_before_connected(void) {
    link.reset();
    link.begin();
    adapter.getRawBuffer().clear();

    TestPayload p = {42, 1.0f};
    link.sendData(static_cast<uint8_t>(MessageType::Data), p);

    // Nothing should have been written to the adapter.
    TEST_ASSERT_EQUAL_INT(0, adapter.available());
    link.reset();
    adapter.getRawBuffer().clear();
}

// ---------------------------------------------------------------------------
// Handshake exchange — duplex two-peer scenarios
// ---------------------------------------------------------------------------

/** @test Both sides reach WAIT_FOR_SYNC after mutual begin(). */
void test_mutual_begin_both_connect(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, linkA.state());
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, linkB.state());
    TEST_ASSERT_TRUE(linkA.connected());
    TEST_ASSERT_TRUE(linkB.connected());
}

/** @test Only one side calls begin(); the other (basic mode) still connects. */
void test_one_sided_begin_connects(void) {
    setUpDuplex();
    linkA.begin(); // only A initiates
    pumpUntilIdle();
    // A must reach WAIT_FOR_SYNC (B replied to A's HS(v=0) even in basic mode).
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, linkA.state());
    TEST_ASSERT_TRUE(linkA.connected());
}

/** @test After connecting, data exchange works in both directions. */
void test_data_exchange_after_handshake(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();

    // A sends to B
    static bool bReceived = false;
    bReceived = false;
    linkB.onDataReceived([](const TestPayload& p) {
        if (p.uptime == 0xABCD) bReceived = true;
    });

    TestPayload outgoing = {0xABCD, 2.0f};
    linkA.sendData(static_cast<uint8_t>(MessageType::Data), outgoing);
    pumpUntilIdle();

    TEST_ASSERT_TRUE(bReceived);
}

// ---------------------------------------------------------------------------
// Handshake retry in CONNECTING
// ---------------------------------------------------------------------------

/** @test Handshake is re-sent after HANDSHAKE_RETRY_MS without a reply. */
void test_handshake_retry_after_timeout(void) {
    // Use duplex adapters so A's writes go to B's buffer, not looped back.
    setUpDuplex();
    linkA.begin();
    // Discard the initial HS that went to B's buffer.
    dAdapterB.getRawBuffer().clear();

    // Just under the retry threshold — no retry yet.
    dAdapterA.advanceMillis(999);
    linkA.update();
    TEST_ASSERT_EQUAL_INT(0, dAdapterB.available());

    // Cross the threshold.
    dAdapterA.advanceMillis(2); // total = 1001 ms
    linkA.update();
    // A should have sent a retry HS; B's buffer now has it.
    TEST_ASSERT_GREATER_THAN(0, dAdapterB.available());
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, linkA.state());
}

/** @test Handshake is NOT re-sent before the retry interval expires. */
void test_no_premature_retry(void) {
    setUpDuplex();
    linkA.begin();
    dAdapterB.getRawBuffer().clear(); // discard the initial HS

    dAdapterA.advanceMillis(500); // well below 1000 ms
    linkA.update();

    TEST_ASSERT_EQUAL_INT(0, dAdapterB.available());
}

// ---------------------------------------------------------------------------
// State transitions during receive
// ---------------------------------------------------------------------------

/** @test State is IN_FRAME while the engine is accumulating frame bytes. */
void test_in_frame_state_during_receive(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    // Inject a few non-zero bytes without a closing delimiter.
    uint8_t bytes[] = {0x05, 0x44, 0x00}; // partial: code + type, no 0x00 yet
    // Inject only non-zero leading bytes (no delimiter yet).
    adapter.inject(bytes, 2);
    link.update();

    TEST_ASSERT_EQUAL(TinyState::IN_FRAME, link.state());

    // Finish with a delimiter to clean up.
    adapter.inject(bytes + 2, 1); // inject 0x00
    link.update();
    link.reset();
    adapter.getRawBuffer().clear();
}

/** @test State returns to WAIT_FOR_SYNC after a complete (valid) frame. */
void test_state_returns_to_wait_for_sync_after_frame(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    TestPayload p = {1, 1.0f};
    link.sendData(static_cast<uint8_t>(MessageType::Data), p);
    while (adapter.available() > 0) link.update();
    link.flush();

    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, link.state());
}

/** @test FRAME_COMPLETE is the state seen by a synchronous receive callback. */
void test_frame_complete_visible_in_callback(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    static TinyState stateInCallback;
    stateInCallback = TinyState::WAIT_FOR_SYNC;

    link.onDataReceived([](const TestPayload&) {
        stateInCallback = link.state();
    });

    TestPayload p = {7, 7.0f};
    link.sendData(static_cast<uint8_t>(MessageType::Data), p);
    while (adapter.available() > 0) link.update();

    TEST_ASSERT_EQUAL(TinyState::FRAME_COMPLETE, stateInCallback);
    link.reset();
    adapter.getRawBuffer().clear();
}

// ---------------------------------------------------------------------------
// AWAITING_ACK
// ---------------------------------------------------------------------------

/** @test sendData() in handshake mode transitions to AWAITING_ACK. */
void test_awaiting_ack_after_send(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();
    TEST_ASSERT_TRUE(linkA.connected());

    TestPayload p = {10, 1.0f};
    linkA.sendData(static_cast<uint8_t>(MessageType::Data), p);

    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, linkA.state());
}

/** @test Receiving an Ack while AWAITING_ACK returns state to WAIT_FOR_SYNC. */
void test_ack_clears_awaiting_ack(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();

    TestPayload p = {20, 2.0f};
    linkA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, linkA.state());

    // Manually send an ACK from B back to A.
    TinyAck ack;
    ack.seq    = linkA.seq();
    ack.result = TinyStatus::STATUS_OK;

    // Build and inject the ACK frame directly into A's RX buffer.
    const size_t PLAIN = 3 + sizeof(TinyAck) + 2;
    const size_t ENC   = PLAIN + 4;
    uint8_t pBuf[PLAIN];
    uint8_t rawBuf[ENC];
    size_t plainLen = tinylink::packet::pack(
        message_type_to_wire(MessageType::Ack), 0,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), pBuf, PLAIN);
    size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen, rawBuf, ENC);
    uint8_t zero = 0x00;
    dAdapterA.inject(&zero, 1);
    dAdapterA.inject(rawBuf, eLen);
    dAdapterA.inject(&zero, 1);

    linkA.update();
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, linkA.state());
}

/** @test AWAITING_ACK times out and returns to WAIT_FOR_SYNC. */
void test_awaiting_ack_timeout(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();

    TestPayload p = {30, 3.0f};
    linkA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, linkA.state());

    // Advance time past the default 250 ms timeout with no Ack arriving.
    dAdapterA.advanceMillis(300);
    linkA.update();

    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, linkA.state());
    TEST_ASSERT_EQUAL_UINT32(1, linkA.getStats().timeout);
}

/** @test Data frames received while AWAITING_ACK are still dispatched. */
void test_data_received_while_awaiting_ack(void) {
    setUpDuplex();
    linkA.begin();
    linkB.begin();
    pumpUntilIdle();

    // A sends to B (→ AWAITING_ACK), then B sends to A immediately.
    static bool aReceived = false;
    aReceived = false;
    linkA.onDataReceived([](const TestPayload& p) {
        if (p.uptime == 99) aReceived = true;
    });

    TestPayload toB = {1, 1.0f};
    linkA.sendData(static_cast<uint8_t>(MessageType::Data), toB);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, linkA.state());

    TestPayload toA = {99, 9.9f};
    linkB.sendData(static_cast<uint8_t>(MessageType::Data), toA);
    pumpUntilIdle();

    TEST_ASSERT_TRUE_MESSAGE(aReceived,
        "Data frame not dispatched while A was in AWAITING_ACK");
    // A is still AWAITING_ACK (Ack for A's packet never arrived).
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, linkA.state());
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

/** @test reset() clears handshake state and restores WAIT_FOR_SYNC. */
void test_reset_clears_handshake_state(void) {
    link.reset();
    link.begin();
    TEST_ASSERT_EQUAL(TinyState::CONNECTING, link.state());
    link.reset();
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, link.state());
    TEST_ASSERT_TRUE(link.connected()); // basic mode after reset
}

/** @test reset() clears stats. */
void test_reset_clears_stats(void) {
    link.reset();
    // Inject a bad frame to generate a CRC error.
    uint8_t junk[] = {0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0x00};
    adapter.inject(junk, sizeof(junk));
    link.update();
    // CRC counter might or might not be non-zero, depending on decode result;
    // what matters is that reset() zeroes it.
    link.reset();
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().crc);
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().timeout);
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().overflow);
    adapter.getRawBuffer().clear();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_handshake_tests(void) {
    // begin() basics
    RUN_TEST(test_begin_sets_connecting_state);
    RUN_TEST(test_connected_false_while_connecting);
    RUN_TEST(test_connected_true_basic_mode);
    RUN_TEST(test_send_blocked_before_connected);

    // Two-peer handshake
    RUN_TEST(test_mutual_begin_both_connect);
    RUN_TEST(test_one_sided_begin_connects);
    RUN_TEST(test_data_exchange_after_handshake);

    // Retry
    RUN_TEST(test_handshake_retry_after_timeout);
    RUN_TEST(test_no_premature_retry);

    // State transitions
    RUN_TEST(test_in_frame_state_during_receive);
    RUN_TEST(test_state_returns_to_wait_for_sync_after_frame);
    RUN_TEST(test_frame_complete_visible_in_callback);

    // AWAITING_ACK
    RUN_TEST(test_awaiting_ack_after_send);
    RUN_TEST(test_ack_clears_awaiting_ack);
    RUN_TEST(test_awaiting_ack_timeout);
    RUN_TEST(test_data_received_while_awaiting_ack);

    // reset()
    RUN_TEST(test_reset_clears_handshake_state);
    RUN_TEST(test_reset_clears_stats);
}
