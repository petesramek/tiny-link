#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <stdint.h>

struct MyData {
    uint32_t uptime;
    float temperature;
    uint8_t commandId;
} __attribute__((packed)); // Forces both CPUs to use the same memory layout

#endif
