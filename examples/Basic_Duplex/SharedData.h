#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <stdint.h>

struct MyData {
    uint32_t uptime;
    float temperature;
    uint8_t commandId;
};

#endif
