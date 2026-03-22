#include "Packet.h"
#include "Fletcher16.h"
#include <string.h>

namespace tinylink {
namespace packet {

size_t pack(uint8_t type, uint8_t seq,
            const uint8_t* payload, size_t payloadLen,
            uint8_t* out, size_t outCap)
{
    size_t need = packet_size(payloadLen);
    if (!out || outCap < need) return 0;

    out[0] = type;
    out[1] = seq;
    out[2] = (uint8_t)payloadLen;
    if (payloadLen && payload) memcpy(&out[3], payload, payloadLen);

    // compute checksum over header+payload
    uint16_t chk = tinylink::codec::fletcher16(out, 3 + payloadLen);
    out[3 + payloadLen + 0] = (uint8_t)(chk & 0xFF);
    out[3 + payloadLen + 1] = (uint8_t)((chk >> 8) & 0xFF);
    return need;
}

size_t unpack(const uint8_t* src, size_t srcLen,
              uint8_t* outType, uint8_t* outSeq,
              uint8_t* outPayload, size_t outPayloadCap)
{
    if (!src || srcLen < 5) return 0; // min header + checksum

    uint8_t payloadLen = src[2];
    size_t expected = packet_size(payloadLen);
    if (srcLen != expected) return 0;

    uint16_t recv = (uint16_t)src[3 + payloadLen + 0] | (uint16_t(src[3 + payloadLen + 1]) << 8);
    uint16_t calc = tinylink::codec::fletcher16(src, 3 + payloadLen);
    if (calc != recv) return 0;

    if (outType) *outType = src[0];
    if (outSeq) *outSeq = src[1];

    if (outPayload) {
        if (outPayloadCap < payloadLen) return 0;
        if (payloadLen) memcpy(outPayload, &src[3], payloadLen);
    }

    return payloadLen;
}

} // namespace packet
} // namespace tinylink
