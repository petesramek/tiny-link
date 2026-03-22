#ifndef TINYLINK_PACKET_H
#define TINYLINK_PACKET_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {
namespace packet {

// helper: packet length = 3 (hdr) + payloadLen + 2 (fletcher)
static inline size_t packet_size(size_t payloadLen) {
    return 3 + payloadLen + 2;
}

// Pack a packet into out (outCap >= packet_size(payloadLen)). Returns bytes written or 0 on error.
size_t pack(uint8_t type, uint8_t seq,
            const uint8_t* payload, size_t payloadLen,
            uint8_t* out, size_t outCap);

// Unpack a packet. On success returns payload length (>0) and fills outType/outSeq/outPayload (if non-null).
// Returns 0 on error (malformed, length mismatch, checksum failure, or insufficient outPayloadCap).
size_t unpack(const uint8_t* src, size_t srcLen,
              uint8_t* outType, uint8_t* outSeq,
              uint8_t* outPayload, size_t outPayloadCap);

} // namespace packet
} // namespace tinylink

#endif // TINYLINK_PACKET_H
