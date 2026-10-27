#ifndef TINYLINK_COMPILE_CONTRACT_ADAPTER_H
#define TINYLINK_COMPILE_CONTRACT_ADAPTER_H

#include "TinyLink.h"

struct CompileContractAdapter {
    bool isOpen();
    int available();
    int read();
    tinylink::TinyWriteResult write(uint8_t);
    tinylink::TinyWriteResult write(const uint8_t*, size_t);
    unsigned long millis();
};

#endif
