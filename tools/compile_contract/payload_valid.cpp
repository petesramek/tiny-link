// Must compile: a trivially copyable payload at the maximum size.
#include "TinyLink.h"
#include "test_tinylink/FakeAdapter.h"

struct ValidPayload {
    uint8_t bytes[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE];
};

template class tinylink::TinyLink<ValidPayload, tinylink_test::FakeAdapter>;
