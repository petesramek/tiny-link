#include "CompileAdapter.h"

/** A user-defined destructor makes this payload invalid for byte serialization. */
struct NonTrivialPayload {
    uint8_t value;
    ~NonTrivialPayload() {}
};

int main() {
    CompileContractAdapter adapter;
    tinylink::TinyLink<NonTrivialPayload, CompileContractAdapter> invalid(adapter);
    (void)invalid;
}
