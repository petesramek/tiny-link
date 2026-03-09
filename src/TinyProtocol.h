#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include <stdint.h>
#include <string.h>

const uint8_t SOH = 0x01, STX = 0x02, ETX = 0x03, ACK = 0x06, NAK = 0x15, CAN = 0x18;

const uint8_t TYPE_DATA = 'D', TYPE_DEBUG = 'g', TYPE_REQ = 'R', TYPE_DONE = 'K';

enum class TinyStatus { STATUS_OK = 0, ERR_TIMEOUT, ERR_CRC };

enum class TinyState { 
    WAIT_SOH, IN_TYPE, WAIT_SEQ, WAIT_LEN, WAIT_H_CHK, 
    WAIT_STX, IN_PAYLOAD, WAIT_ETX, WAIT_P_CHK, WAIT_ACK 
};

struct TinyStats {
    uint16_t packets;  
    uint16_t crcErrs;  
    uint16_t timeouts; 
};

#endif
