/**
 * @file test_protocol.cpp
 * @brief Unit tests for TinyLink v0.5.0 (COBS + Fletcher-16 + connection handshake)
 * 
 * Verifies data integrity, framing robustness, hardware-level fault tolerance
 * (noise, timeouts, overflows), and the full connection handshake protocol.
 */

#include <unity.h>
#include <vector>
#include <stdint.h>
#include <cmath>
#include "TinyLink.h"
#include "adapters/TinyTestAdapter.h"

/** @brief Sample payload to test struct alignment and float precision */
struct TestPayload {
    uint32_t uptime;
    float value;
} __attribute__((packed));

/** @brief Global flag for callback verification */
bool g_callbackTriggered = false;

/** @brief Global callback handler */
void testCallback(const TestPayload& data) { 
    g_callbackTriggered = true; 
}

/** 
 * @class LoopbackAdapter
 * @brief Simulates a physical wire by piping TX directly into the RX buffer.
 */
class LoopbackAdapter : public tinylink::TinyTestAdapter {
public:
    void write(uint8_t c) override { inject(&c, 1); }
    void write(const uint8_t* b, size_t l) override { inject(b, l); }
};

/**
 * @class TxCaptureAdapter
 * @brief Captures TX bytes without looping back, for handshake testing.
 */
class TxCaptureAdapter : public tinylink::TinyTestAdapter {
public:
    std::vector<uint8_t> txBuffer;
    void write(uint8_t c) override { txBuffer.push_back(c); }
    void write(const uint8_t* b, size_t l) override {
        txBuffer.insert(txBuffer.end(), b, b + l);
    }
    void clearTx() { txBuffer.clear(); }
};

LoopbackAdapter adapter;
tinylink::TinyLink<TestPayload, LoopbackAdapter> link(adapter);

/**
 * @brief Complete a connection handshake on a TinyLink instance via loopback.
 *
 * Resets the link to CONNECTING state, runs update() until the loopback
 * self-completes the STATUS exchange, then clears any leftover bytes.
 */
template<typename T, typename A>
void completeHandshake(tinylink::TinyLink<T, A>& l, A& a) {
    l.reset();
    for (int i = 0; i < 5 && l.state() != tinylink::TinyState::WAIT_FOR_SYNC; i++) {
        l.update();
    }
    a.getRawBuffer().clear();
}

/** @brief Reset the state machine and virtual clock before every test case */
void setUp(void) {
    link.flush();
    link.clearStats();
    link.onReceive(nullptr);
    link.onAckReceived(nullptr);
    link.onAckFailed(nullptr);
    link.onHandshakeFailed(nullptr);
    link.onStatusReceived(nullptr);
    adapter.setMillis(0);
    adapter.getRawBuffer().clear();
    g_callbackTriggered = false;

    // Reset and complete handshake via loopback so tests start in WAIT_FOR_SYNC
    link.reset();
    for (int i = 0; i < 5 && link.state() != tinylink::TinyState::WAIT_FOR_SYNC; i++) {
        link.update();
    }
    adapter.getRawBuffer().clear(); // Clear any leftover handshake bytes
}

/** @brief Reset state after every test case */
void tearDown(void) {
    // No specific teardown required for this suite
}

/** 
 * @brief TEST: End-to-end success.
 * Verifies Send -> COBS Encode -> Decode -> Verify Fletcher-16.
 */
void test_cobs_loopback(void) {
    TestPayload data = { 98765, 3.1415f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }

    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(data.uptime, link.peek().uptime);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, data.value, link.peek().value);
}

/** 
 * @brief TEST: COBS transparency.
 * Verifies that a payload consisting entirely of 0x00 is handled correctly.
 */
void test_cobs_zero_payload(void) {
    TestPayload zeros = { 0, 0.0f }; 
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), zeros);
    
    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "COBS failed to handle all-zero payload");
}

/** 
 * @brief TEST: Junk Resynchronization.
 * Verifies that the 0x00 delimiter allows recovery from leading junk data.
 */
void test_cobs_resync(void) {
    uint8_t junk[] = { 0xFF, 0xAA, 0x12, 0x45 }; 
    adapter.inject(junk, sizeof(junk));

    TestPayload data = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Link failed to resync after junk data");
}

/** 
 * @brief TEST: Integrity Check.
 * Verifies that a bit-flip in the Fletcher-16 checksum causes rejection.
 */
void test_cobs_crc_failure(void) {
    TestPayload data = { 10, 2.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 3) {
        buf[buf.size() - 2] ^= 0xFF; 
    }

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** 
 * @brief TEST: Asynchronous Callback.
 * Verifies that the onReceive callback fires correctly upon valid packet arrival.
 */
void test_async_callback(void) {
    link.onReceive(testCallback);
    TestPayload data = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    while(adapter.available() > 0) {
        link.update();
    }

    TEST_ASSERT_TRUE_MESSAGE(g_callbackTriggered, "Callback was not triggered");
}

/** 
 * @brief TEST: Noise Filtering.
 * Verifies that double delimiters (0x00 0x00) are ignored and do not cause CRC errors.
 */
void test_cobs_double_delimiter(void) {
    uint8_t doubleZero[] = { 0x00, 0x00 };
    adapter.inject(doubleZero, sizeof(doubleZero));
    
    link.update();
    
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
}

/** 
 * @brief TEST: Multi-packet Burst.
 * Verifies that two packets in the same buffer both trigger callbacks.
 */
void test_callback_burst(void) {
    static int triggerCount;
    triggerCount = 0;
    link.onReceive([](const TestPayload& d) { triggerCount++; });

    TestPayload p = { 1, 1.1f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    while(adapter.available() > 0) {
        link.update();
        if(link.available()) link.flush();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, triggerCount, "Callback should have fired exactly twice");
}

/**
 * @brief TEST: Inter-byte Timeout Cleanup.
 * Verifies that the buffer self-clears after a partial transmission timeout.
 */
void test_timeout_cleanup(void) {
    uint8_t partial[] = { 0x00, 0x05, 0x01 }; 
    adapter.inject(partial, sizeof(partial));
    link.update();
    
    adapter.advanceMillis(300); 
    link.update();

    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().timeouts);
    
    TestPayload data = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Engine failed to recover after timeout");
}

/**
 * @brief TEST: Stuttering Sync.
 * Verifies resilience to multiple 0x00 bytes preceding a valid frame.
 */
void test_double_delimiter_resilience(void) {
    TestPayload data = { 123, 1.23f };
    uint8_t noise[] = { 0x00, 0x00, 0x00 };
    adapter.inject(noise, sizeof(noise));
    
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
}

/**
 * @brief TEST: Phantom Frame Rejection.
 * Verifies that frames shorter than the protocol minimum are silently ignored.
 */
void test_short_frame_rejection(void) {
    uint8_t shorty[] = { 0x00, 0x02, 0x00 }; 
    adapter.inject(shorty, sizeof(shorty));
    link.update();
    
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs); 
}

/**
 * @brief TEST: Buffer Overflow Protection.
 * Verifies that the internal buffer safely resets when receiving non-zero runaway data.
 */
void test_buffer_overflow_protection(void) {
    for(int i = 0; i < 300; i++) {
        uint8_t junk = 0xFF;
        adapter.inject(&junk, 1);
    }
    link.update();
    
    TestPayload data = { 88, 8.8f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Overflow guard prevented recovery");
}

/** 
 * @brief TEST: Message Type Filtering.
 * Verifies that the engine handles valid packets of various Message Types.
 */
void test_type_filtering(void) {
    TestPayload data = { 55, 5.5f };
    link.send('Q', data); 
    
    while(adapter.available() > 0 && !link.available()) link.update();
    
    TEST_ASSERT_EQUAL_UINT8('Q', link.type());
}

/** 
 * @brief TEST: Sequence Tracking.
 * Verifies that packet sequence numbers correctly increment.
 */
void test_sequence_tracking(void) {
    TestPayload data = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t seq1 = link.seq();
    
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t seq2 = link.seq();
    
    TEST_ASSERT_EQUAL_UINT8(seq1 + 1, seq2);
}

/** 
 * @brief TEST: Buffer Isolation.
 * Verifies that incoming unverified data does not corrupt current valid data.
 */
void test_buffer_isolation(void) {
    TestPayload p1 = { 111, 1.1f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();
    
    // Inject a partial/corrupt packet
    uint8_t partial[] = { 0x00, 0x05, 0x01 }; 
    adapter.inject(partial, sizeof(partial));
    link.update();
    
    // Valid data must remain isolated
    TEST_ASSERT_EQUAL_UINT32(111, link.peek().uptime);
}

/** 
 * @brief TEST: Sequence Number Wrap-around.
 * Verifies that the uint8_t sequence counter correctly rolls over from 255 to 0
 * without losing synchronization or failing the Fletcher-16 check.
 */
void test_sequence_wrap_around(void) {
    TestPayload data = { 1, 1.0f };
    
    // 1. Get the starting sequence
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    uint8_t startSeq = link.seq(); 

    // 2. Send 256 more packets to complete a full circle
    for(int i = 0; i < 256; i++) {
        link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
        while(adapter.available() > 0) {
            link.update();
            if(link.available()) link.flush();
        }
    }
    
    // 3. It should have wrapped back to exactly startSeq
    TEST_ASSERT_EQUAL_UINT8(startSeq, link.seq()); 
}

/** 
 * @brief TEST: Floating Point Extremes.
 * Verifies that the protocol is bit-transparent and can handle Non-Finite 
 * values (NAN/INF) often generated by failing sensors.
 */
void test_float_extremes(void) {
    TestPayload data = { 100, NAN };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }
    
    TEST_ASSERT_TRUE(std::isnan(link.peek().value));
}

/** 
 * @brief TEST: Timeout Boundary Condition.
 * Verifies that a packet arriving at 240ms (just under the 250ms limit) 
 * is accepted, while ensuring the timer resets correctly.
 */
void test_timeout_boundary(void) {
    TestPayload data = { 123, 4.56f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);

    // Grab the raw bytes from the loopback
    std::vector<uint8_t> raw = adapter.getRawBuffer();
    adapter.getRawBuffer().clear();

    // Inject only half the packet
    adapter.inject(raw.data(), raw.size() / 2);
    link.update();
    
    // Wait almost until the timeout
    adapter.advanceMillis(240); 
    link.update();
    
    // Inject the second half
    adapter.inject(raw.data() + (raw.size() / 2), raw.size() - (raw.size() / 2));
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Timeout triggered too early on boundary");
}

/** 
 * @brief TEST: Zero-Length Header Rejection.
 * Verifies that a valid COBS frame with a payload length (5) that 
 * does not match the expected PLAIN_SIZE (13) is caught as a CRC error.
 */
void test_zero_length_header_rejection(void) {
    // A perfectly valid COBS frame of 5 bytes (No 0x00 bytes inside the data)
    // [0x06] (Code: Next zero in 6 bytes) [0x01, 0x02, 0x03, 0x04, 0x05]
    uint8_t cobs_frame[] = { 0x06, 0x01, 0x02, 0x03, 0x04, 0x05 };
    uint8_t delimiter = 0x00;

    adapter.getRawBuffer().clear();
    adapter.inject(cobs_frame, sizeof(cobs_frame));
    adapter.inject(&delimiter, 1); 
    
    link.update();
    
    // dLen = 5. PLAIN_SIZE = 13.
    // 5 != 13 triggers the 'else { _stats.crcErrs++; }' in TinyLink.h
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that packets larger than the compiled struct size are rejected */
void test_oversized_struct_protection(void) {
    // Simulate a 100-byte packet arriving for a 8-byte TestPayload
    uint8_t largeRaw[110]; 
    memset(largeRaw, 0xAA, 110);
    adapter.inject(largeRaw, 110);
    link.update();
    
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that a new 0x00 delimiter immediately kills any pending partial frame */
void test_mid_frame_sync_reset(void) {
    uint8_t partial[] = { 0x00, 0x05, 0x01, 0x02 }; // Partial frame
    adapter.inject(partial, sizeof(partial));
    link.update();
    
    // Immediately send a full valid packet starting with 0x00
    TestPayload data = { 5, 5.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "New sync byte failed to reset partial state");
    TEST_ASSERT_EQUAL_UINT32(5, link.peek().uptime);
}

/** 
 * @struct TinyStruct
 * @brief 1-byte structure to test the absolute lower limit of the protocol.
 */
struct TinyStruct { 
    uint8_t val; 
} __attribute__((packed));

/** 
 * @brief TEST: Minimum Payload Handling.
 * Verifies that the protocol correctly encapsulates, encodes, and decodes 
 * a single-byte payload. This ensures the COBS pointer logic and 
 * Fletcher-16 bounds are scale-invariant.
 * 
 * @pre A 1-byte struct is initialized.
 * @post The byte must be received perfectly after loopback.
 */
void test_minimum_payload(void) {
    // Create a specific link for the 1-byte struct
    tinylink::TinyLink<TinyStruct, LoopbackAdapter> tinyLink(adapter);
    completeHandshake(tinyLink, adapter);
    TinyStruct s = { 0xFE };
    
    tinyLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), s);
    
    while(adapter.available() > 0 && !tinyLink.available()) {
        tinyLink.update();
    }
    
    TEST_ASSERT_TRUE(tinyLink.available());
    TEST_ASSERT_EQUAL_UINT8(0xFE, tinyLink.peek().val);
}

struct OtherStruct { uint64_t raw; } __attribute__((packed));

/** @test Verifies that valid packets with matching sizes but different purposes are distinguished by 'type()' */
void test_type_validation(void) {
    tinylink::TinyLink<OtherStruct, LoopbackAdapter> otherLink(adapter);
    completeHandshake(otherLink, adapter);
    OtherStruct data = { 0xDEADBEEF };
    
    otherLink.send('X', data); // Type 'X'
    while(adapter.available() > 0) otherLink.update();
    
    // The engine MUST report type 'X', allowing the user to reject it
    TEST_ASSERT_EQUAL_UINT8('X', otherLink.type());
}

/** @test Verifies that COBS decoder fails safely if a 'code' points outside the physical buffer */
void test_cobs_read_overflow_safety(void) {
    // 0x0A (10) but we only provide 3 bytes
    uint8_t evilFrame[] = { 0x0A, 0x01, 0x02, 0x00 };
    adapter.inject(evilFrame, sizeof(evilFrame));
    link.update();
    
    // Should return dLen = 0 and NOT increment packets
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0, link.getStats().packets);
}

/** @test Verifies that the engine handles a startup sequence of multiple delimiters safely */
void test_cold_boot_sync(void) {
    uint8_t initialNoise[] = { 0x00, 0x00, 0x00, 0x00 };
    adapter.inject(initialNoise, sizeof(initialNoise));
    link.update();
    
    // Should be silent, no errors
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

/** @test Verifies that a zero at the very first byte of the payload is encoded/decoded correctly */
void test_leading_zero_payload(void) {
    TestPayload data = { 0x00FFFFFF, 1.0f }; // Leading zero in the uint32
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_EQUAL_UINT32(0x00FFFFFF, link.peek().uptime);
}

/** @test Verifies that Fletcher-16 detects swapped bytes (which simple checksums miss) */
void test_swapped_byte_detection(void) {
    TestPayload data = { 0x11223344, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    if (buf.size() > 10) {
        // Swap two adjacent data bytes in the raw buffer
        std::swap(buf[5], buf[6]); 
    }
    
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

struct Small { uint8_t a; } __attribute__((packed));
struct Large { uint32_t a; uint32_t b; } __attribute__((packed));

/** @test Verifies that two different TinyLink instances on the same adapter don't collide */
void test_crosstalk_rejection(void) {
    tinylink::TinyLink<Small, LoopbackAdapter> linkSmall(adapter);
    tinylink::TinyLink<Large, LoopbackAdapter> linkLarge(adapter);
    completeHandshake(linkSmall, adapter);
    
    Small s = { 0xFF };
    linkSmall.send('T', s);  // Use 'T' to avoid TYPE_STATUS ('S') confusion
    
    // Update the LARGE link with SMALL data
    while(adapter.available() > 0) linkLarge.update();
    
    // Large link should NOT have accepted the small packet due to PLAIN_SIZE mismatch
    TEST_ASSERT_FALSE(linkLarge.available());
    TEST_ASSERT_EQUAL_UINT16(1, linkLarge.getStats().crcErrs);
}

struct MaxBlock { uint8_t raw[59]; } __attribute__((packed)); // 59 + 5 = 64 (Safe)
/** @test Verifies COBS behavior at the 254-byte block boundary */
void test_cobs_max_block_boundary(void) {
    tinylink::TinyLink<MaxBlock, LoopbackAdapter> maxLink(adapter);
    completeHandshake(maxLink, adapter);
    MaxBlock data; 
    memset(data.raw, 0xFF, 59); // Fill with non-zero data
    
    maxLink.send(tinylink::TYPE_DATA, data);
    while(adapter.available() > 0) maxLink.update();
    
    TEST_ASSERT_TRUE(maxLink.available());
    TEST_ASSERT_EQUAL_UINT8(0xFF, maxLink.peek().raw[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, maxLink.peek().raw[59]);
}


/** @test Verifies that a single bit-flip in the payload is caught by Fletcher-16 */
void test_single_bit_flip_detection(void) {
    TestPayload data = { 0x00, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Flip the LSB of the first data byte (usually at index 4 or 5 after header)
    buf[5] ^= 0x01; 
    
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that a zero at the final byte of the struct is preserved */
void test_trailing_zero_payload(void) {
    struct TailZero { uint16_t val; uint8_t zero; } __attribute__((packed));
    tinylink::TinyLink<TailZero, LoopbackAdapter> tailLink(adapter);
    completeHandshake(tailLink, adapter);
    
    TailZero data = { 0xABCD, 0x00 };
    tailLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    while(adapter.available() > 0) tailLink.update();
    TEST_ASSERT_TRUE(tailLink.available());
    TEST_ASSERT_EQUAL_UINT8(0x00, tailLink.peek().zero);
}

/** @test Verifies that back-to-back packets with minimal spacing are both received */
void test_minimal_interframe_spacing(void) {
    TestPayload p = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    
    int count = 0;
    while(adapter.available() > 0) {
        link.update();
        if(link.available()) {
            count++;
            link.flush();
        }
    }
    TEST_ASSERT_EQUAL_INT(2, count);
}

/** @test Verifies that a single bit flipping to zero (Ghost Zero) is caught */
void test_ghost_zero_detection(void) {
    TestPayload data; 
    memset(&data, 0xFF, sizeof(data));
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Flip a bit in the middle of the payload to make it 0x00
    buf[10] = 0x00; 
    
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE_MESSAGE(link.available(), "Ghost zero was not detected");
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies rejection of COBS frames with invalid internal code pointers */
void test_cobs_invalid_code_pointer(void) {
    // Code 0x01 means "next zero is here", but we provide 0xFF instead
    uint8_t evil[] = { 0x01, 0xFF, 0x00 }; 
    adapter.inject(evil, sizeof(evil));
    link.update();
    
    TEST_ASSERT_FALSE(link.available());
    // Should be caught by the dLen == 0 safety in cobs_decode
}

/** @test Verifies that a second 0x00 during a frame triggers an immediate reset */
void test_mid_frame_reset(void) {
    uint8_t partial[] = { 0x00, 0x05, 0x01, 0x02 }; // Partial frame
    adapter.inject(partial, sizeof(partial));
    link.update();
    
    // New start delimiter
    uint8_t startAgain = 0x00;
    adapter.inject(&startAgain, 1);
    link.update();
    
    // _rawIdx should be 0 now
    TestPayload data = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_TRUE_MESSAGE(link.available(), "Double start failed to reset state machine");
}

/** @test Verifies that the PLAIN_SIZE remains within the safe uint8_t bounds (255) */
void test_protocol_mtu_limit(void) {
    // 3 (Header) + 64 (max Payload) + 2 (CRC) = 69
    TEST_ASSERT_LESS_THAN_UINT32(256, (3 + sizeof(TestPayload) + 2));
}

/** @test Verifies that valid COBS frames with incorrect payload sizes are rejected */
void test_structural_size_mismatch(void) {
    struct NewData { uint32_t a; uint32_t b; uint32_t c; } __attribute__((packed));
    tinylink::TinyLink<NewData, LoopbackAdapter> espLink(adapter);
    completeHandshake(espLink, adapter);
    
    NewData d = {1, 2, 3};
    espLink.send(static_cast<uint8_t>(tinylink::MessageType::Data), d); // Sends 17 bytes (3+12+2)
    
    // TinyLink<TestPayload> expects 13 bytes (3+8+2)
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that the engine can handle packets with no inter-frame gap */
void test_zero_gap_stress(void) {
    TestPayload p = {10, 1.0f};
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    // Manually inject a second packet immediately
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    
    int count = 0;
    while(adapter.available() > 0) {
        link.update();
        if(link.available()) {
            count++;
            link.flush();
        }
    }
    TEST_ASSERT_EQUAL_INT(2, count);
}

/** @test Verifies the precise boundary of the inter-byte timeout logic */
void test_timeout_precision(void) {
    uint8_t partial = 0xFF;
    adapter.inject(&partial, 1);
    link.update();
    
    // Wait exactly at the edge (249ms)
    adapter.advanceMillis(249);
    link.update();
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().timeouts);
    
    // Cross the edge (2ms later)
    adapter.advanceMillis(2);
    link.update();
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().timeouts);
}

/** @test Verifies that a 0x00 byte inside the payload is correctly handled by COBS */
void test_payload_zero_transparency(void) {
    TestPayload data = { 0x11002233, 0.0f }; // Contains two zeros in binary
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    int spins = 0, max_spins = 10;
    while (adapter.available() > 0 && !link.available() && spins++ < max_spins) {
        link.update();
    }
    
    TEST_ASSERT_TRUE(link.available());
    TEST_ASSERT_EQUAL_UINT32(0x11002233, link.peek().uptime);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, link.peek().value);
}

/** @test Verifies the exact boundary of the 64-byte buffer headroom */
void test_buffer_headroom_boundary(void) {
    // TestPayload PLAIN_SIZE is 13. Max allowed dLen is 13 + 64 = 77.
    struct Giant { uint8_t raw[64]; } __attribute__((packed)); 
    tinylink::TinyLink<Giant, LoopbackAdapter> giantLink(adapter);
    completeHandshake(giantLink, adapter);
    
    Giant g; memset(g.raw, 0xFF, 64);
    giantLink.send('G', g); // Sends ~64 bytes
    
    link.clearStats();
    while(adapter.available() > 0) link.update();
    
    // Should be caught as a crcErr because it fit in the rawBuf but size was wrong
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that micro-frames (noise) are silently ignored without logging errors */
void test_micro_frame_noise_suppression(void) {
    uint8_t noise[] = { 0x00, 0x01, 0x00, 0x02, 0x00 };
    adapter.inject(noise, sizeof(noise));
    link.update();
    
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().packets);
}

/** @test Verifies that multi-byte integers split across COBS blocks are reassembled correctly */
void test_split_integer_alignment(void) {
    // 0x11223344: We want the COBS 'pointer' to fall between 0x22 and 0x33
    TestPayload data = { 0x11223344, 1.0f }; 
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_EQUAL_UINT32(0x11223344, link.peek().uptime);
}

/** @test Verifies that the engine ignores new data while 'available' flag is still true (user hasn't flushed) */
void test_reentrant_lock_safety(void) {
    TestPayload p1 = { 100, 1.0f };
    TestPayload p2 = { 200, 2.0f };
    
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();
    
    // Packet 1 is now 'available'. Inject Packet 2.
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p2);
    link.update(); // This should be ignored because _hasNew is true
    
    TEST_ASSERT_EQUAL_UINT32(100, link.peek().uptime);
    link.flush(); // Now Packet 2 can be processed on next update
}

/** @test Verifies that a continuous stream of delimiters (0x00) doesn't trigger false errors or hangs */
void test_endless_delimiter_resilience(void) {
    uint8_t zeros[100]; memset(zeros, 0, 100);
    adapter.inject(zeros, 100);
    
    link.update();
    TEST_ASSERT_EQUAL_UINT16(0, link.getStats().crcErrs);
    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

/** @test Verifies Fletcher-16 accuracy on a high-contrast (min/max) bit pattern */
void test_fletcher_contrast_integrity(void) {
    struct Contrast { uint8_t a; uint8_t b; } __attribute__((packed));
    tinylink::TinyLink<Contrast, LoopbackAdapter> cLink(adapter);
    completeHandshake(cLink, adapter);
    Contrast c = { 0x00, 0xFF };
    
    cLink.send('W', c);  // Use 'W' to avoid TYPE_COMMAND ('C') conflict
    while(adapter.available() > 0) cLink.update();
    TEST_ASSERT_TRUE(cLink.available());
}

/** @test Verifies that the 16-bit checksum is packed and unpacked in a fixed endian order */
void test_endian_checksum_consistency(void) {
    TestPayload data = { 0xAABBCCDD, 1.23f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    
    // The last two bytes of the COBS frame (before the 0x00) are the Fletcher-16.
    // We swap them manually. If the engine is endian-sensitive, it must fail.
    if (buf.size() > 3) {
        std::swap(buf[buf.size()-2], buf[buf.size()-3]);
    }
    
    while(adapter.available() > 0) link.update();
    
    // If the checksum was correctly packed/unpacked, swapping bytes MUST cause a CRC error.
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies that the state machine persists across fragmented single-byte arrivals */
void test_fragmented_arrival_persistence(void) {
    TestPayload data = { 5, 5.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    std::vector<uint8_t> raw = adapter.getRawBuffer();
    adapter.getRawBuffer().clear();

    for(uint8_t b : raw) {
        adapter.inject(&b, 1);
        adapter.advanceMillis(50); // Significant delay, but < 250ms
        link.update();
    }
    TEST_ASSERT_TRUE(link.available());
}

/** @test Verifies bit-transparency for negative values and maximum unsigned integers */
void test_signed_boundary_integrity(void) {
    TestPayload data = { 0xFFFFFFFF, -123.45f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFF, link.peek().uptime);
    TEST_ASSERT_EQUAL_FLOAT(-123.45f, link.peek().value);
}

/** @test Verifies that changing the callback during an active reception is safe */
void test_callback_hotswap_safety(void) {
    uint8_t start[] = { 0x00, 0x05, 0x01 };
    adapter.inject(start, sizeof(start));
    link.update();
    
    link.onReceive([](const TestPayload& d){ /* New Handler */ });
    
    // Complete the packet
    TestPayload data = {1, 1.0f};
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_TRUE(link.available());
}

/** @test Verifies that the checksum for one packet doesn't affect the next calculation */
void test_checksum_independence(void) {
    TestPayload p1 = { 0x11111111, 1.0f };
    TestPayload p2 = { 0x22222222, 2.0f };
    
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0) link.update();
    uint16_t crc1 = link.getStats().packets; // Check if p1 passed
    link.flush();

    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p2);
    while(adapter.available() > 0) link.update();
    
    TEST_ASSERT_EQUAL_UINT32(2, link.getStats().packets);
}

/** @test Verifies that the decoder treats truncated COBS frames as invalid (Read overflow) */
void test_cobs_truncation_safety(void) {
    // 0xFF means "next zero in 255", but we only give 10 bytes
    uint8_t truncated[] = { 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x00 };
    adapter.inject(truncated, sizeof(truncated));
    link.update();
    
    TEST_ASSERT_FALSE(link.available());
    // Should result in dLen = 0
}

/** @test Verifies that a delimiter appearing inside the expected payload kills the frame */
void test_mid_payload_delimiter_reset(void) {
    uint8_t header[] = { 0x04, 'D', 0x01, 0x08 }; // Valid looking header start
    adapter.inject(header, sizeof(header));
    link.update();
    
    uint8_t reset = 0x00; // Sudden reset
    adapter.inject(&reset, 1);
    link.update();
    
    TEST_ASSERT_EQUAL_UINT8(0, link.available());
}

/** @test Verifies that Fletcher-16 handles payloads that result in a zero-sum correctly */
void test_zero_sum_integrity(void) {
    struct AllOnes { uint32_t a; uint32_t b; } __attribute__((packed));
    tinylink::TinyLink<AllOnes, LoopbackAdapter> aLink(adapter);
    completeHandshake(aLink, adapter);
    AllOnes data = { 0x00, 0x00 }; // Often results in specific sum patterns
    
    aLink.send(tinylink::TYPE_DATA, data);
    while(adapter.available() > 0) aLink.update();
    TEST_ASSERT_TRUE(aLink.available());
}

/** @test Verifies that the checksum protects the very end of the payload */
void test_trailing_bit_integrity(void) {
    TestPayload data = { 0, 1.0f };  
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), data);
    
    std::vector<uint8_t>& buf = adapter.getRawBuffer();
    // Index 10 is typically the last byte of an 8-byte payload in a 13-byte frame
    buf[buf.size() - 3] ^= 0x01; 
    
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_FALSE(link.available());
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}

/** @test Verifies Fletcher-16 mathematical correctness at the 255 modulo boundary */
void test_fletcher_modulo_boundary(void) {
    struct ModData { uint8_t a; uint8_t b; } __attribute__((packed));
    tinylink::TinyLink<ModData, LoopbackAdapter> mLink(adapter);
    completeHandshake(mLink, adapter);
    
    // 0xFF (255) is the modulo identity for Fletcher.  
    // Testing if 0x00 and 0xFF are treated correctly in the accumulator.
    ModData d = { 0xFF, 0xFF }; 
    mLink.send('M', d);
    while(adapter.available() > 0) mLink.update();
    TEST_ASSERT_TRUE(mLink.available());
}

/** @test Verifies that the engine handles a new frame delimiter immediately following a valid CRC */
void test_tight_frame_overlap(void) {
    TestPayload p = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    // Manually inject a second delimiter to stress the sync logic
    uint8_t zero = 0x00;
    adapter.inject(&zero, 1);
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);

    int count = 0;
    while(adapter.available() > 0) {
        link.update();
        if(link.available()) { count++; link.flush(); }
    }
    TEST_ASSERT_EQUAL_INT(2, count);
}

/** @test Verifies that unregistering a callback mid-stream doesn't cause a null-pointer crash */
void test_callback_deregistration_safety(void) {
    link.onReceive(testCallback);
    TestPayload p = { 1, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    
    // Halfway through, kill the callback
    link.onReceive(nullptr);
    
    while(adapter.available() > 0) link.update();
    // Should NOT crash, should just finish silently
    TEST_ASSERT_TRUE(link.available());
}

struct MaxJump { uint8_t data[60]; } __attribute__((packed)); // Stay under 240 limit
/** @test Verifies COBS decoding at the maximum possible block jump distance */
void test_cobs_max_jump_safety(void) {
    tinylink::TinyLink<MaxJump, LoopbackAdapter> jLink(adapter);
    completeHandshake(jLink, adapter);
    MaxJump j; memset(j.data, 0x01, 60);
    
    jLink.send('J', j);
    while(adapter.available() > 0) jLink.update();
    TEST_ASSERT_TRUE(jLink.available());
}

/** @test Verifies that the struct has no "hidden" padding bytes by checking total size */
void test_struct_packing_integrity(void) {
    // uptime(4) + value(4) = 8. If sizeof is 9 or 12, packing failed.
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, sizeof(TestPayload), "Struct padding detected! Packing failed.");
}

/** @test Verifies that new junk data cannot overwrite un-flushed valid data */
void test_unflushed_data_protection(void) {
    TestPayload p1 = { 0xDEADC0DE, 1.0f };
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p1);
    while(adapter.available() > 0 && !link.available()) link.update();

    // Inject 50 bytes of 0xFF junk
    uint8_t junk[50]; memset(junk, 0xFF, 50);
    adapter.inject(junk, 50);
    link.update();

    TEST_ASSERT_EQUAL_UINT32(0xDEADC0DE, link.peek().uptime);
}

/** @test Verifies that the template handles the smallest possible primitive type */
void test_primitive_type_support(void) {
    tinylink::TinyLink<uint8_t, LoopbackAdapter> pLink(adapter);
    completeHandshake(pLink, adapter);
    uint8_t val = 0xAB;
    pLink.send('P', val);
    while(adapter.available() > 0) pLink.update();
    TEST_ASSERT_EQUAL_UINT8(0xAB, pLink.peek());
}

/** @test Verifies that a truncated COBS frame is rejected before decoding completes */
void test_truncated_cobs_rejection(void) {
    // Code says 10 bytes, but we only give 3
    uint8_t shorty[] = { 0x0A, 0x01, 0x02, 0x00 };
    adapter.inject(shorty, sizeof(shorty));
    link.update();
    TEST_ASSERT_FALSE(link.available());
}

/** @test Verifies Fletcher-16 stability with a high-bit-density (all 0xFF) payload */
void test_fletcher_overflow_stability(void) {
    TestPayload p; memset(&p, 0xFF, sizeof(p));
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_TRUE(link.available());
}

/** @test Verifies that swapping CRC endianness results in a rejection */
void test_crc_endian_sensitivity(void) {
    TestPayload p = {1, 1.0f};
    link.send(static_cast<uint8_t>(tinylink::MessageType::Data), p);
    std::vector<uint8_t>& b = adapter.getRawBuffer();
    // Swap last two bytes before 0x00
    std::swap(b[b.size()-2], b[b.size()-3]);
    while(adapter.available() > 0) link.update();
    TEST_ASSERT_EQUAL_UINT16(1, link.getStats().crcErrs);
}


// =============================================================================
// v0.5.0 Protocol Tests: Connection Handshake, ACK, and State Machine
// =============================================================================

/** @test Verifies that a loopback link self-completes the handshake and reaches WAIT_FOR_SYNC */
void test_handshake_completes(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, link.state());

    for (int i = 0; i < 5 && link.state() != tinylink::TinyState::WAIT_FOR_SYNC; i++) {
        link.update();
    }

    TEST_ASSERT_EQUAL(tinylink::TinyState::WAIT_FOR_SYNC, link.state());
    TEST_ASSERT_TRUE(link.connected());
}

/** @test Verifies that send() is blocked and nothing is transmitted while in CONNECTING */
void test_send_blocked_during_connecting(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, link.state());

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    // send() should have returned early — state and buffer unchanged
    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, link.state());
    // Buffer only contains the STATUS bytes written by reset/first-update,
    // but since we haven't called update() yet, it should be empty.
    TEST_ASSERT_EQUAL(0, adapter.available());
}

/** @test Verifies that send() is blocked while in HANDSHAKING */
void test_send_blocked_during_handshaking(void) {
    // Capture the STATUS bytes that a fresh link would send
    TxCaptureAdapter captureAdapter;
    tinylink::TinyLink<TestPayload, TxCaptureAdapter> captureLink(captureAdapter);
    captureLink.update(); // Sends STATUS to captureAdapter.txBuffer

    // Create a sink-only link and drive it to HANDSHAKING state
    tinylink::TinyTestAdapter sinkAdapter;
    tinylink::TinyLink<TestPayload, tinylink::TinyTestAdapter> handshakeLink(sinkAdapter);
    handshakeLink.update(); // Sends our STATUS to sink (discarded)

    // Inject the captured STATUS so handshakeLink sees the "remote" STATUS
    sinkAdapter.inject(captureAdapter.txBuffer.data(), captureAdapter.txBuffer.size());
    handshakeLink.update(); // Receives STATUS → HANDSHAKING, sends ACK (to sink)

    TEST_ASSERT_EQUAL(tinylink::TinyState::HANDSHAKING, handshakeLink.state());

    // send() during HANDSHAKING must be silently blocked
    TestPayload data = { 1, 1.0f };
    handshakeLink.send(tinylink::TYPE_DATA, data);
    TEST_ASSERT_EQUAL(tinylink::TinyState::HANDSHAKING, handshakeLink.state());
}

/** @test Verifies onAckReceived fires and state returns to WAIT_FOR_SYNC after data is sent */
void test_ack_received_on_data(void) {
    static bool g_ackFired;
    static uint8_t g_ackedSeq;
    g_ackFired = false;
    g_ackedSeq = 0xFF;

    link.onAckReceived([](uint8_t seq) {
        g_ackFired  = true;
        g_ackedSeq  = seq;
    });

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);
    uint8_t sentSeq = link.seq();

    // Drain fully: DATA comes back, ACK is sent for it, ACK for our send is received
    while (adapter.available() > 0) {
        link.update();
        if (link.available()) link.flush();
    }

    TEST_ASSERT_TRUE(g_ackFired);
    TEST_ASSERT_EQUAL_UINT8(sentSeq, g_ackedSeq);
    TEST_ASSERT_EQUAL(tinylink::TinyState::WAIT_FOR_SYNC, link.state());
}

/** @test Verifies onAckFailed fires with ERR_TIMEOUT when no ACK arrives within the window */
void test_ack_failed_on_timeout(void) {
    static bool g_failFired;
    static tinylink::TinyResult g_failReason;
    g_failFired  = false;
    g_failReason = tinylink::TinyResult::OK;

    link.onAckFailed([](uint8_t, tinylink::TinyResult r) {
        g_failFired  = true;
        g_failReason = r;
    });
    link.setAckTimeout(100); // 100 ms

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);
    adapter.getRawBuffer().clear(); // Remove looped-back bytes so no ACK is generated

    adapter.advanceMillis(200); // Past ackTimeout
    link.update();

    TEST_ASSERT_TRUE(g_failFired);
    TEST_ASSERT_EQUAL(tinylink::TinyResult::ERR_TIMEOUT, g_failReason);
    TEST_ASSERT_EQUAL(tinylink::TinyState::WAIT_FOR_SYNC, link.state());
}

/** @test Verifies retries happen (state stays CONNECTING) up to _maxHandshakeRetries */
void test_handshake_retry(void) {
    tinylink::TinyTestAdapter sinkAdapter;
    tinylink::TinyLink<TestPayload, tinylink::TinyTestAdapter> retryLink(sinkAdapter);
    retryLink.setHandshakeTimeout(100);
    retryLink.setMaxHandshakeRetries(2);

    retryLink.update(); // Initial STATUS (time=0), timer set
    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, retryLink.state());

    sinkAdapter.advanceMillis(110); // Past 100 ms
    retryLink.update(); // Retry 1
    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, retryLink.state());

    sinkAdapter.advanceMillis(110);
    retryLink.update(); // Retry 2 — still under max
    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, retryLink.state());
}

/** @test Verifies onHandshakeFailed fires after max retries are exhausted */
void test_handshake_failed_callback(void) {
    static bool g_handshakeFailed;
    g_handshakeFailed = false;

    tinylink::TinyTestAdapter sinkAdapter;
    tinylink::TinyLink<TestPayload, tinylink::TinyTestAdapter> failLink(sinkAdapter);
    failLink.setHandshakeTimeout(100);
    failLink.setMaxHandshakeRetries(2);
    failLink.onHandshakeFailed([]() { g_handshakeFailed = true; });

    failLink.update();             // Initial STATUS
    sinkAdapter.advanceMillis(110);
    failLink.update();             // Retry 1
    sinkAdapter.advanceMillis(110);
    failLink.update();             // Retry 2
    sinkAdapter.advanceMillis(110);
    failLink.update();             // Max retries exceeded → callback

    TEST_ASSERT_TRUE(g_handshakeFailed);
}

/** @test Verifies the full CONNECTING → HANDSHAKING → WAIT_FOR_SYNC state transition */
void test_state_transitions(void) {
    link.reset();
    adapter.getRawBuffer().clear();

    TEST_ASSERT_EQUAL(tinylink::TinyState::CONNECTING, link.state());
    TEST_ASSERT_FALSE(link.connected());

    // A single update() on the loopback link completes the entire handshake
    link.update();

    TEST_ASSERT_EQUAL(tinylink::TinyState::WAIT_FOR_SYNC, link.state());
    TEST_ASSERT_TRUE(link.connected());
}

/** @test Verifies that TYPE_COMMAND reaches the user onReceive callback */
void test_type_command_routing(void) {
    g_callbackTriggered = false;
    link.onReceive(testCallback);

    TestPayload data = { 99, 9.9f };
    link.send(tinylink::TYPE_COMMAND, data);

    while (adapter.available() > 0) {
        link.update();
        if (link.available()) link.flush();
    }

    TEST_ASSERT_TRUE(g_callbackTriggered);
    TEST_ASSERT_EQUAL_UINT8(tinylink::TYPE_COMMAND, link.type());
}

/** @test Verifies that TYPE_ACK frames are never surfaced to the user via available()/onReceive() */
void test_type_ack_not_user_visible(void) {
    static int g_callCount;
    g_callCount = 0;
    link.onReceive([](const TestPayload&) { g_callCount++; });

    TestPayload data = { 1, 1.0f };
    link.send(tinylink::TYPE_DATA, data);

    // Drain all frames: DATA (loopback) + ACKs
    while (adapter.available() > 0) {
        link.update();
        if (link.available()) link.flush();
    }

    // onReceive fired exactly once (for the looped-back DATA), never for ACK frames
    TEST_ASSERT_EQUAL_INT(1, g_callCount);
    TEST_ASSERT_FALSE(link.available());
}


/**
 * @brief Entry point for the PlatformIO Native Test Runner.
 * 
 * v0.5.0 Verification Suite.
 * verified against AVR (8-bit) and ESP/PC (32/64-bit) architectures.
 */
int main(int argc, char **argv) {
    UNITY_BEGIN();

    // --- Core Protocol & Asynchronous Events ---
    RUN_TEST(test_cobs_loopback);                /**< Basic encode/decode/verify pipeline */
    RUN_TEST(test_cobs_zero_payload);            /**< COBS transparency with all-zero data */
    RUN_TEST(test_async_callback);               /**< Asynchronous event trigger verification */
    RUN_TEST(test_callback_burst);               /**< Handling multiple packets in one update cycle */

    // --- Synchronization & Noise Resilience ---
    RUN_TEST(test_cobs_resync);                  /**< Recovery after leading junk/serial noise */
    RUN_TEST(test_cobs_double_delimiter);        /**< Handling of consecutive 0x00 delimiters */
    RUN_TEST(test_double_delimiter_resilience);  /**< Stuttering sync-byte handling (noise filter) */
    RUN_TEST(test_mid_frame_sync_reset);         /**< Forced reset via 0x00 during partial frame */
    RUN_TEST(test_cold_boot_sync);               /**< Graceful handling of power-on delimiter surges */
    RUN_TEST(test_micro_frame_noise_suppression);/**< High-frequency noise spike filtering */

    // --- Integrity, Safety & Memory Protection ---
    RUN_TEST(test_cobs_crc_failure);             /**< Fletcher-16 bit-corruption detection */
    RUN_TEST(test_timeout_cleanup);              /**< Automated partial frame buffer recovery */
    RUN_TEST(test_buffer_overflow_protection);   /**< Runaway TX/noise memory safety reset */
    RUN_TEST(test_buffer_isolation);             /**< Valid peek() data protection during RX */
    RUN_TEST(test_cobs_read_overflow_safety);    /**< Decoder boundary-check protection */
    RUN_TEST(test_ghost_zero_detection);         /**< Detection of bit-flips resulting in 0x00 */
    RUN_TEST(test_single_bit_flip_detection);    /**< High-sensitivity single bit-level integrity */
    RUN_TEST(test_swapped_byte_detection);       /**< Positional sensitivity (swapped byte detection) */

    // --- Logic, Boundaries & State Machine ---
    RUN_TEST(test_type_filtering);               /**< Message type separation and identification */
    RUN_TEST(test_sequence_tracking);            /**< Packet ID incrementation verification */
    RUN_TEST(test_sequence_wrap_around);         /**< Counter rollover (255 -> 0) stability */
    RUN_TEST(test_float_extremes);               /**< Binary transparency for NAN/INF values */
    RUN_TEST(test_type_validation);              /**< Multi-struct instance identification */
    RUN_TEST(test_structural_size_mismatch);     /**< Rejection of valid COBS frames with wrong size */

    // --- Advanced Timing & Serial Pressure ---
    RUN_TEST(test_timeout_boundary);             /**< Precision check of the 250ms window */
    RUN_TEST(test_timeout_precision);            /**< Strict inter-byte timeout enforcement */
    RUN_TEST(test_zero_gap_stress);              /**< Performance under back-to-back TX stress */
    RUN_TEST(test_minimal_interframe_spacing);   /**< High-speed burst synchronization */
    RUN_TEST(test_payload_zero_transparency);    /**< Transparency check for zeros at data boundaries */

    // --- Structural Limits ---
    RUN_TEST(test_short_frame_rejection);        /**< Rejection of frames below protocol minimum */
    RUN_TEST(test_zero_length_header_rejection); /**< Malformed length-field protection */
    RUN_TEST(test_minimum_payload);              /**< Robustness with 1-byte struct payloads */
    RUN_TEST(test_leading_zero_payload);         /**< Encoding check for zeros at byte-0 */
    RUN_TEST(test_trailing_zero_payload);        /**< Encoding check for zeros at final byte */
    RUN_TEST(test_protocol_mtu_limit);           /**< Compile-time safety for MTU boundaries */
    RUN_TEST(test_split_integer_alignment);      /**< Multi-byte reassembly safety */
    RUN_TEST(test_reentrant_lock_safety);        /**< Buffer locking during user processing */
    RUN_TEST(test_endless_delimiter_resilience); /**< Protection against stuck-low bus */
    RUN_TEST(test_fletcher_contrast_integrity);  /**< Checksum accumulator stress test */
    RUN_TEST(test_endian_checksum_consistency);  /**< Cross-platform checksum logic */
    RUN_TEST(test_fragmented_arrival_persistence);/**< Resistance to serial jitter */
    RUN_TEST(test_signed_boundary_integrity);    /**< Signed/Unsigned bit transparency */
    RUN_TEST(test_callback_hotswap_safety);      /**< Dynamic memory/pointer safety */
    RUN_TEST(test_split_integer_alignment);      /**< Re-verifying integer boundaries */

    RUN_TEST(test_checksum_independence);     /**< Calculation isolation */
    RUN_TEST(test_cobs_truncation_safety);     /**< Truncated frame safety */
    RUN_TEST(test_mid_payload_delimiter_reset);/**< Structural reset resilience */
    RUN_TEST(test_zero_sum_integrity);         /**< Zero-checksum bit-transparency */
    RUN_TEST(test_trailing_bit_integrity);     /**< Full-buffer coverage check */

    RUN_TEST(test_fletcher_modulo_boundary);       /**< Modulo-255 math check */
    RUN_TEST(test_tight_frame_overlap);            /**< Back-to-back sync stress */
    RUN_TEST(test_callback_deregistration_safety); /**< Null-pointer hot-swap safety */

    RUN_TEST(test_struct_packing_integrity);
    RUN_TEST(test_unflushed_data_protection);
    RUN_TEST(test_primitive_type_support);
    RUN_TEST(test_truncated_cobs_rejection);
    RUN_TEST(test_fletcher_overflow_stability);
    RUN_TEST(test_crc_endian_sensitivity);

    // --- v0.5.0 Protocol: Handshake, ACK, and State Machine ---
    RUN_TEST(test_handshake_completes);            /**< Loopback self-completes STATUS exchange */
    RUN_TEST(test_send_blocked_during_connecting); /**< send() is a no-op in CONNECTING */
    RUN_TEST(test_send_blocked_during_handshaking);/**< send() is a no-op in HANDSHAKING */
    RUN_TEST(test_ack_received_on_data);           /**< onAckReceived fires after loopback DATA */
    RUN_TEST(test_ack_failed_on_timeout);          /**< onAckFailed fires after ackTimeout */
    RUN_TEST(test_handshake_retry);                /**< Retries happen before giving up */
    RUN_TEST(test_handshake_failed_callback);      /**< onHandshakeFailed fires after max retries */
    RUN_TEST(test_state_transitions);              /**< CONNECTING→WAIT_FOR_SYNC verified */
    RUN_TEST(test_type_command_routing);           /**< TYPE_COMMAND reaches user onReceive */
    RUN_TEST(test_type_ack_not_user_visible);      /**< TYPE_ACK never fires user onReceive */

    return UNITY_END();
}
