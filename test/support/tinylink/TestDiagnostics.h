#ifndef TINYLINK_TEST_TESTDIAGNOSTICS_H
#define TINYLINK_TEST_TESTDIAGNOSTICS_H

#include <stdarg.h>
#include <stdio.h>

namespace tinylink_test {

    /**
     * Formats failure context for TEST_ASSERT_*_MESSAGE inside loops, e.g.
     * `TEST_ASSERT_EQUAL_UINT8_MESSAGE(e, a, diag("index=%u length=%u", i, n))`.
     *
     * The text lives in one static buffer that is overwritten by the next
     * call, so use it directly as the message argument and never store the
     * pointer. Not thread-safe; the native suites are single-threaded.
     * Arguments follow printf rules: cast size_t to unsigned (or use %lu with
     * unsigned long) to stay portable. Output is truncated to fit.
     */
    inline const char* diag(const char* format, ...)
    {
        static char buffer[192];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

} // namespace tinylink_test

#endif // TINYLINK_TEST_TESTDIAGNOSTICS_H
