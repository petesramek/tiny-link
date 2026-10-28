// Must NOT compile: payload with a user-provided copy constructor.
#include "TinyLink.h"
#include "test_tinylink/FakeAdapter.h"

struct InvalidPayload {
    uint8_t value;
    InvalidPayload(const InvalidPayload& other) : value(other.value) {}
};

template class tinylink::TinyLink<InvalidPayload, tinylink_test::FakeAdapter>;
