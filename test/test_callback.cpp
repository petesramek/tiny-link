/**
 * @file test_callback.cpp
 * @brief Regression tests for callback-mode multi-frame reception.
 *
 * Verifies that registering an onDataReceived callback does not stall the RX
 * engine after the first packet. The engine must continue processing
 * subsequent frames without requiring the user to call flush().
 */

#include <unity.h>
#include "globals.h"
#include "adapters/TinyDuplexTestAdapter.h"
#include "internal/protocol/HandshakeMessage.h"
#include "internal/protocol/AckMessage.h"
#include "internal/protocol/LogMessage.h"
#include "internal/codec/CobsCodec.h"
#include "internal/codec/Packet.h"

using namespace tinylink;

/**
 * @brief TEST: Callback-mode continuous reception.
 *
 * Feeds two valid encoded frames to a TinyLink instance with a registered
 * onDataReceived callback. Asserts the callback is invoked exactly twice,
 * proving that the engine does not stall after the first packet.
 *
 * This is a regression test for the bug where _hasNew was set to true even
 * in callback mode, causing update() to short-circuit on subsequent calls.
 */
void test_callback_receives_multiple_frames(void) {
    static int callCount;
    callCount = 0;

    link.onDataReceived([](const TestPayload&) { callCount++; });

    TestPayload p = { 42, 1.5f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, callCount,
        "Callback must fire for every frame; engine must not stall after first callback");
}

/**
 * @brief TEST: Callback-mode does not set available() flag.
 *
 * When a callback is registered, the engine must NOT set _hasNew, so that
 * available() returns false after reception (the callback already handled
 * the data; no polling is needed).
 */
void test_callback_mode_does_not_set_available(void) {
    link.onDataReceived([](const TestPayload&) { /* handled */ });

    TestPayload p = { 7, 0.1f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE_MESSAGE(link.available(),
        "Callback mode must not set available(); polling flag must remain clear");
}

/**
 * @brief TEST: Polling mode still sets available() flag when no callback registered.
 *
 * Confirms that removing the callback (nullptr) restores normal polling behaviour.
 */
void test_polling_mode_sets_available_without_callback(void) {
    /* No callback registered (setUp already cleared it) */

    TestPayload p = { 99, 9.9f };
    link.sendData(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while (adapter.available() > 0 && !link.available()) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(),
        "Polling mode must set available() when no callback is registered");
}

// ---------------------------------------------------------------------------
// onLogReceived
// ---------------------------------------------------------------------------

/**
 * @brief TEST: onLogReceived fires when an incoming log frame is received.
 */
void test_on_log_received_fires(void) {
    static bool logFired;
    static uint16_t receivedCode;
    logFired = false;
    receivedCode = 0;

    link.onLogReceived([](const LogMessage& msg) {
        logFired = true;
        receivedCode = msg.code;
    });

    link.sendLog(LogLevel::INFO, 0x1234, "hello");

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(logFired, "onLogReceived must fire for a Log frame");
    TEST_ASSERT_EQUAL_UINT16(0x1234, receivedCode);
}

/**
 * @brief TEST: Log frames do not reach the data callback.
 */
void test_log_frame_not_dispatched_as_data(void) {
    static bool dataFired;
    dataFired = false;

    link.onDataReceived([](const TestPayload&) { dataFired = true; });

    link.sendLog(LogLevel::WARN, 0, "test");

    while (adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE_MESSAGE(dataFired,
        "Log frames must not be dispatched to the data callback");
}

// ---------------------------------------------------------------------------
// onHandshakeReceived
// ---------------------------------------------------------------------------

/**
 * @brief TEST: onHandshakeReceived fires on both HS(v=0) and HS(v=1).
 */
void test_on_handshake_received_fires(void) {
    static TinyDuplexTestAdapter dAdA, dAdB;
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lA(dAdA);
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lB(dAdB);

    dAdA.connect(dAdB);
    dAdB.connect(dAdA);
    dAdA.setMillis(0);
    dAdB.setMillis(0);
    dAdA.getRawBuffer().clear();
    dAdB.getRawBuffer().clear();
    lA.reset();
    lB.reset();

    static int hsCount;
    hsCount = 0;
    lB.onHandshakeReceived([](const HandshakeMessage&) { hsCount++; });

    lA.begin(); // sends HS(v=0) to B's buffer
    lA.update();
    lB.update(); // B receives HS(v=0) → callback + sends HS(v=1)
    lA.update(); // A receives HS(v=1)

    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(1, hsCount,
        "onHandshakeReceived must fire when a handshake frame is received");
}

// ---------------------------------------------------------------------------
// onAckReceived
// ---------------------------------------------------------------------------

/**
 * @brief TEST: onAckReceived fires when an ACK frame is received while AWAITING_ACK.
 */
void test_on_ack_received_fires(void) {
    static TinyDuplexTestAdapter dAdA2, dAdB2;
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lA2(dAdA2);
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lB2(dAdB2);

    dAdA2.connect(dAdB2);
    dAdB2.connect(dAdA2);
    dAdA2.setMillis(0);
    dAdB2.setMillis(0);
    dAdA2.getRawBuffer().clear();
    dAdB2.getRawBuffer().clear();
    lA2.reset();
    lB2.reset();

    // Connect both sides
    lA2.begin(); lB2.begin();
    for (int i = 0; i < 10; ++i) { lA2.update(); lB2.update(); }

    static bool ackFired;
    ackFired = false;
    lA2.onAckReceived([](const TinyAck&) { ackFired = true; });

    // A sends data → goes to AWAITING_ACK
    TestPayload p = { 1, 1.0f };
    lA2.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA2.state());

    // Build and inject an ACK from B back to A directly.
    // Plain frame layout: [wireType:1][seq:1][reserved:1][payload][fcs:2] = 3+N+2
    // COBS overhead: up to 4 extra bytes for large frames.
    TinyAck ack;
    ack.seq    = lA2.seq();
    ack.result = TinyStatus::STATUS_OK;

    const size_t PLAIN = 3 + sizeof(TinyAck) + 2; // header(3) + payload + fcs(2)
    const size_t ENC   = PLAIN + 4;               // COBS worst-case overhead
    uint8_t pBuf[PLAIN];
    uint8_t rawBuf[ENC];
    size_t plainLen = tinylink::packet::pack(
        message_type_to_wire(MessageType::Ack), 0,
        reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), pBuf, PLAIN);
    size_t eLen = tinylink::codec::cobs_encode(pBuf, plainLen, rawBuf, ENC);
    uint8_t zero = 0x00;
    dAdA2.inject(&zero, 1);
    dAdA2.inject(rawBuf, eLen);
    dAdA2.inject(&zero, 1);

    lA2.update();

    TEST_ASSERT_TRUE_MESSAGE(ackFired, "onAckReceived must fire when an ACK frame is received");
    TEST_ASSERT_EQUAL(TinyState::WAIT_FOR_SYNC, lA2.state());
}

// ---------------------------------------------------------------------------
// enableAutoUpdate / autoUpdateISR
// ---------------------------------------------------------------------------

/**
 * @brief TEST: autoUpdateISR calls update() on the registered instance.
 *
 * enableAutoUpdate() registers the TinyLink instance for static dispatch.
 * Calling autoUpdateISR() (as an ISR would) must drive the engine as if
 * update() had been called directly.
 */
void test_auto_update_isr_drives_engine(void) {
    static int isr_callCount;
    isr_callCount = 0;

    link.onDataReceived([](const TestPayload&) { isr_callCount++; });
    link.enableAutoUpdate();

    TestPayload p = { 55, 5.5f };
    link.sendData(static_cast<uint8_t>(MessageType::Data), p);

    // Simulate ISR calls instead of manual update() calls.
    while (adapter.available() > 0) {
        tinylink::TinyLink<TestPayload, LoopbackAdapter>::autoUpdateISR();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, isr_callCount,
        "autoUpdateISR must drive the engine exactly like update()");
}

/**
 * @brief TEST: autoUpdateISR works without any prior enableAutoUpdate() call.
 *
 * The constructor auto-registers the instance, so attaching autoUpdateISR()
 * to any interrupt source must work immediately after construction — no
 * extra setup step is required.
 */
void test_auto_update_isr_works_without_enable_call(void) {
    static int noEnable_callCount;
    noEnable_callCount = 0;

    // Use a fresh pair so the auto-registration from construction is the only
    // registration — no enableAutoUpdate() is called anywhere below.
    static LoopbackAdapter freshAdapter;
    static tinylink::TinyLink<TestPayload, LoopbackAdapter> freshLink(freshAdapter);
    freshLink.reset();
    freshAdapter.getRawBuffer().clear();

    freshLink.onDataReceived([](const TestPayload&) { noEnable_callCount++; });

    TestPayload p = { 77, 7.7f };
    freshLink.sendData(static_cast<uint8_t>(MessageType::Data), p);

    // Drive via ISR without having called enableAutoUpdate().
    while (freshAdapter.available() > 0) {
        tinylink::TinyLink<TestPayload, LoopbackAdapter>::autoUpdateISR();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, noEnable_callCount,
        "autoUpdateISR must work without calling enableAutoUpdate() first");
}

/**
 * @brief TEST: enableAutoUpdate() re-registers a different instance for ISR dispatch.
 *
 * When two instances of the same <T,Adapter> type exist, the last-constructed
 * one is registered by default.  enableAutoUpdate() on the first instance
 * must switch ISR dispatch back to it.
 */
void test_enable_auto_update_switches_instance(void) {
    static int countA, countB;
    countA = 0;
    countB = 0;

    static LoopbackAdapter adA, adB;
    // instA is constructed first; instB last → instB is the default ISR target.
    static tinylink::TinyLink<TestPayload, LoopbackAdapter> instA(adA);
    static tinylink::TinyLink<TestPayload, LoopbackAdapter> instB(adB);
    instA.reset(); adA.getRawBuffer().clear();
    instB.reset(); adB.getRawBuffer().clear();

    instA.onDataReceived([](const TestPayload&) { countA++; });
    instB.onDataReceived([](const TestPayload&) { countB++; });

    // instB is the default ISR target after construction.
    TestPayload p = { 1, 1.0f };
    instB.sendData(static_cast<uint8_t>(MessageType::Data), p);
    while (adB.available() > 0) {
        tinylink::TinyLink<TestPayload, LoopbackAdapter>::autoUpdateISR();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countB,
        "Default ISR target must be last-constructed instance");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, countA,
        "Non-default instance must not receive ISR-driven updates");

    // Explicitly switch to instA.
    instA.enableAutoUpdate();
    instA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    while (adA.available() > 0) {
        tinylink::TinyLink<TestPayload, LoopbackAdapter>::autoUpdateISR();
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, countA,
        "After enableAutoUpdate(), ISR must drive the newly registered instance");
}

void register_callback_tests(void) {
    RUN_TEST(test_callback_receives_multiple_frames);
    RUN_TEST(test_callback_mode_does_not_set_available);
    RUN_TEST(test_polling_mode_sets_available_without_callback);
    RUN_TEST(test_on_log_received_fires);
    RUN_TEST(test_log_frame_not_dispatched_as_data);
    RUN_TEST(test_on_handshake_received_fires);
    RUN_TEST(test_on_ack_received_fires);
    RUN_TEST(test_auto_update_isr_drives_engine);
    RUN_TEST(test_auto_update_isr_works_without_enable_call);
    RUN_TEST(test_enable_auto_update_switches_instance);
}

// ---------------------------------------------------------------------------
// sendAck()
// ---------------------------------------------------------------------------

/**
 * @brief TEST: sendAck() releases the peer from AWAITING_ACK.
 *
 * After A sends data (handshake mode), A enters AWAITING_ACK.  B receives
 * the data and calls sendAck().  A's next update() must exit AWAITING_ACK
 * and return to WAIT_FOR_SYNC without waiting for the timeout.
 */
void test_sendack_clears_peer_awaiting_ack(void) {
    static TinyDuplexTestAdapter dA, dB;
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lA(dA);
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lB(dB);

    dA.connect(dB); dB.connect(dA);
    dA.setMillis(0); dB.setMillis(0);
    dA.getRawBuffer().clear(); dB.getRawBuffer().clear();
    lA.reset(); lB.reset();

    // Both connect via handshake.
    lA.begin(); lB.begin();
    for (int i = 0; i < 10; ++i) { lA.update(); lB.update(); }
    TEST_ASSERT_TRUE(lA.connected());
    TEST_ASSERT_TRUE(lB.connected());

    static bool ackSent;
    ackSent = false;
    lB.onDataReceived([](const TestPayload&) {
        // B acknowledges A's frame immediately.
        lB.sendAck();
        ackSent = true;
    });

    TestPayload p = { 10, 1.0f };
    lA.sendData(static_cast<uint8_t>(MessageType::Data), p);
    TEST_ASSERT_EQUAL(TinyState::AWAITING_ACK, lA.state());

    // One round-trip: B receives data + sends ACK; A receives ACK.
    for (int i = 0; i < 10; ++i) { lB.update(); lA.update(); }

    TEST_ASSERT_TRUE_MESSAGE(ackSent, "B's callback must have fired");
    TEST_ASSERT_EQUAL_MESSAGE(TinyState::WAIT_FOR_SYNC, lA.state(),
        "A must leave AWAITING_ACK once the ACK arrives");
}

/**
 * @brief TEST: Calling sendData() from inside onDataReceived() preserves AWAITING_ACK.
 *
 * This tests the state-overwrite fix: update() must NOT clobber the
 * AWAITING_ACK state that sendData() sets when called from a callback.
 */
void test_reply_from_callback_preserves_awaiting_ack(void) {
    static TinyDuplexTestAdapter dA2, dB2;
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lA2(dA2);
    static TinyLink<TestPayload, TinyDuplexTestAdapter> lB2(dB2);

    dA2.connect(dB2); dB2.connect(dA2);
    dA2.setMillis(0); dB2.setMillis(0);
    dA2.getRawBuffer().clear(); dB2.getRawBuffer().clear();
    lA2.reset(); lB2.reset();

    lA2.begin(); lB2.begin();
    for (int i = 0; i < 10; ++i) { lA2.update(); lB2.update(); }

    static bool replySent;
    replySent = false;
    lB2.onDataReceived([](const TestPayload& d) {
        // B ACKs A's request, then sends a reply of its own.
        lB2.sendAck();
        TestPayload reply = { d.uptime + 1, d.value + 1.0f };
        lB2.sendData(static_cast<uint8_t>(MessageType::Data), reply);
        replySent = true;
    });

    TestPayload req = { 1, 0.0f };
    lA2.sendData(static_cast<uint8_t>(MessageType::Data), req);

    // One round: B processes A's Data, fires callback (ACK + reply), A processes ACK.
    for (int i = 0; i < 10; ++i) { lB2.update(); lA2.update(); }

    TEST_ASSERT_TRUE_MESSAGE(replySent, "B's callback must have fired and sent a reply");
    // B must be in AWAITING_ACK (waiting for A to ACK B's reply) —
    // NOT back in WAIT_FOR_SYNC (which would indicate the state was overwritten).
    TEST_ASSERT_EQUAL_MESSAGE(TinyState::AWAITING_ACK, lB2.state(),
        "B must be in AWAITING_ACK after sending a reply from its callback");
    // A exits AWAITING_ACK after receiving B's ACK.
    TEST_ASSERT_EQUAL_MESSAGE(TinyState::WAIT_FOR_SYNC, lA2.state(),
        "A must have exited AWAITING_ACK after receiving B's ACK");
}

void register_sendack_tests(void) {
    RUN_TEST(test_sendack_clears_peer_awaiting_ack);
    RUN_TEST(test_reply_from_callback_preserves_awaiting_ack);
}

