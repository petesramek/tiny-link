#ifndef TINY_PROTOCOL_H
#define TINY_PROTOCOL_H

#include <stdint.h>
#include <string.h>

// Message Types
const uint8_t TYPE_DATA  = 'D';
const uint8_t TYPE_DEBUG = 'g';
const uint8_t TYPE_REQ   = 'R';
const uint8_t TYPE_DONE  = 'K';

// Protocol Status
enum class TinyStatus { 
    STATUS_OK = 0, 
    ERR_TIMEOUT, 
    ERR_CRC 
};

// Simplified State Machine for COBS
enum class TinyState { 
    WAIT_FOR_SYNC,  // Searching for 0x00 delimiter
    IN_FRAME        // Accumulating bytes until next 0x00
};

// Telemetry
struct TinyStats {
    uint32_t packets;  // Successfully received
    uint16_t crcErrs;  // Checksum/COBS failures
    uint16_t timeouts; // Interrupted frames
};

#endif
