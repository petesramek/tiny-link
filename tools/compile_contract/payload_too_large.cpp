// Must NOT compile: payload exceeds the maximum payload size.
#include "TinyLink.h"
#include "support/tinylink/FakeAdapter.h"

struct OversizedPayload {
    uint8_t bytes[tinylink::packet::PACKET_MAX_PAYLOAD_SIZE + 1];
};

template class tinylink::TinyLink<OversizedPayload, tinylink_test::FakeAdapter>;
