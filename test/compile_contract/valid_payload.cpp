#include "CompileAdapter.h"

/** Trivial packed payload accepted by the TinyLink payload contract. */
struct ValidPayload {
    uint8_t id;
    uint16_t value;
} __attribute__((packed));

/** Exact protocol payload-size limit accepted by the compile contract. */
struct MaximumPayload {
    uint8_t bytes[64];
};

static_assert(tinylink::detail::is_trivially_copyable<ValidPayload>::value,
              "The valid payload probe must be trivially copyable.");
static_assert(sizeof(MaximumPayload) == 64,
              "The maximum-size payload probe must be 64 bytes.");

int main() {
    CompileContractAdapter adapter;
    tinylink::TinyLink<ValidPayload, CompileContractAdapter> valid(adapter);
    tinylink::TinyLink<MaximumPayload, CompileContractAdapter> maximum(adapter);
    (void)valid;
    (void)maximum;
}
