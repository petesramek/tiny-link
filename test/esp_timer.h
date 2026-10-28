#ifndef TINYLINK_TEST_ESP_TIMER_STUB_H
#define TINYLINK_TEST_ESP_TIMER_STUB_H

#include <stdint.h>

static int64_t tinylinkEspStubTimeMicroseconds = 0;

inline int64_t esp_timer_get_time()
{
    return tinylinkEspStubTimeMicroseconds;
}

#endif // TINYLINK_TEST_ESP_TIMER_STUB_H
