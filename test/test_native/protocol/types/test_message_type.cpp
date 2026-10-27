/**
 * @file test_message_type.cpp
 * @brief Unit tests for MessageType helpers (MessageType.h).
 */

#include <unity.h>
#include <string.h>
#include "protocol/MessageType.h"

using namespace tinylink;

/** @test message_type_from_wire accepts 'D' as MessageType::Data. */
void test_mt_from_wire_data(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('D', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Data),
                            static_cast<uint8_t>(out));
}

/** @test message_type_from_wire accepts 'L' as MessageType::Log. */
void test_mt_from_wire_debug(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('L', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Log),
                            static_cast<uint8_t>(out));
}

/** @test message_type_from_wire accepts 'C' as MessageType::Cmd. */
void test_mt_from_wire_cmd(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('C', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Cmd),
                            static_cast<uint8_t>(out));
}

/** @test message_type_from_wire accepts 'A' as MessageType::Ack. */
void test_mt_from_wire_ack(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('A', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Ack),
                            static_cast<uint8_t>(out));
}

/** @test message_type_from_wire accepts 'K' as MessageType::Done. */
void test_mt_from_wire_done(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('K', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Done),
                            static_cast<uint8_t>(out));
}

/** @test message_type_from_wire accepts 'H' as MessageType::Handshake. */
void test_mt_from_wire_handshake(void) {
    MessageType out;
    TEST_ASSERT_TRUE(message_type_from_wire('H', out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MessageType::Handshake),
                            static_cast<uint8_t>(out));
}

/** @test Legacy 'R' is rejected (breaking change). */
void test_mt_from_wire_legacy_r_rejected(void) {
    MessageType out;
    TEST_ASSERT_FALSE(message_type_from_wire('R', out));
}

/** @test Unknown bytes return false. */
void test_mt_from_wire_unknown(void) {
    MessageType out;
    TEST_ASSERT_FALSE(message_type_from_wire(0x00, out));
    TEST_ASSERT_FALSE(message_type_from_wire(0xFF, out));
    TEST_ASSERT_FALSE(message_type_from_wire('X',  out));
}

/** @test message_type_to_wire returns correct byte for each type. */
void test_mt_to_wire(void) {
    TEST_ASSERT_EQUAL_HEX8('D', message_type_to_wire(MessageType::Data));
    TEST_ASSERT_EQUAL_HEX8('L', message_type_to_wire(MessageType::Log));
    TEST_ASSERT_EQUAL_HEX8('C', message_type_to_wire(MessageType::Cmd));
    TEST_ASSERT_EQUAL_HEX8('A', message_type_to_wire(MessageType::Ack));
    TEST_ASSERT_EQUAL_HEX8('K', message_type_to_wire(MessageType::Done));
    TEST_ASSERT_EQUAL_HEX8('H', message_type_to_wire(MessageType::Handshake));
}

/** @test message_type_to_string returns expected label strings. */
void test_mt_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("Data",      message_type_to_string(MessageType::Data));
    TEST_ASSERT_EQUAL_STRING("Log",       message_type_to_string(MessageType::Log));
    TEST_ASSERT_EQUAL_STRING("Cmd",       message_type_to_string(MessageType::Cmd));
    TEST_ASSERT_EQUAL_STRING("Ack",       message_type_to_string(MessageType::Ack));
    TEST_ASSERT_EQUAL_STRING("Done",      message_type_to_string(MessageType::Done));
    TEST_ASSERT_EQUAL_STRING("Handshake", message_type_to_string(MessageType::Handshake));
}

/** @test message_type_to_wire / from_wire round-trip for all defined types. */
void test_mt_roundtrip(void) {
    static const MessageType types[] = {
        MessageType::Data,
        MessageType::Log,
        MessageType::Cmd,
        MessageType::Ack,
        MessageType::Done,
        MessageType::Handshake
    };
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); ++i) {
        uint8_t wire = message_type_to_wire(types[i]);
        MessageType back;
        TEST_ASSERT_TRUE(message_type_from_wire(wire, back));
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(types[i]),
                                static_cast<uint8_t>(back));
    }
}

void register_message_type_tests(void) {
    RUN_TEST(test_mt_from_wire_data);
    RUN_TEST(test_mt_from_wire_debug);
    RUN_TEST(test_mt_from_wire_cmd);
    RUN_TEST(test_mt_from_wire_ack);
    RUN_TEST(test_mt_from_wire_done);
    RUN_TEST(test_mt_from_wire_handshake);
    RUN_TEST(test_mt_from_wire_legacy_r_rejected);
    RUN_TEST(test_mt_from_wire_unknown);
    RUN_TEST(test_mt_to_wire);
    RUN_TEST(test_mt_to_string);
    RUN_TEST(test_mt_roundtrip);
}
