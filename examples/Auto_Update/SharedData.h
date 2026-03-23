#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <stdint.h>

// Shared payload — both sides of the link must use an identical, packed struct.
struct SensorData {
    uint32_t uptime;       // millis() at send time
    float    temperature;  // degrees Celsius
    uint8_t  seq;          // application sequence counter
} __attribute__((packed));

// Message-type tag used when sending data frames.
static const uint8_t TYPE_DATA = static_cast<uint8_t>(tinylink::MessageType::Data);

#endif // SHARED_DATA_H
