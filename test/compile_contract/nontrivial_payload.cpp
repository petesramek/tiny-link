#include "CompileAdapter.h"

struct NonTrivialPayload {
    uint8_t value;
    ~NonTrivialPayload() {}
};

int main() {
    CompileContractAdapter adapter;
    tinylink::TinyLink<NonTrivialPayload, CompileContractAdapter> invalid(adapter);
    (void)invalid;
}
