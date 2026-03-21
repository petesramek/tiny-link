#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>

void setUp(void);
void tearDown(void);

namespace tinytest {

struct State {
    int tests;
    int failures;
    State() : tests(0), failures(0) {}
};

inline State& state() {
    static State s;
    return s;
}

class assertion_failure : public std::runtime_error {
public:
    explicit assertion_failure(const std::string& msg) : std::runtime_error(msg) {}
};

inline void fail(const std::string& msg, const char* file, int line) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << msg;
    throw assertion_failure(oss.str());
}

template <typename E, typename A>
inline void assert_equal(const E& expected, const A& actual,
                         const char* expectedExpr, const char* actualExpr,
                         const char* file, int line) {
    if (!(expected == actual)) {
        std::ostringstream oss;
        oss << "expected " << expectedExpr << " == " << actualExpr;
        fail(oss.str(), file, line);
    }
}

template <typename E, typename A>
inline void assert_not_equal(const E& expected, const A& actual,
                             const char* expectedExpr, const char* actualExpr,
                             const char* file, int line) {
    if (expected == actual) {
        std::ostringstream oss;
        oss << "expected " << expectedExpr << " != " << actualExpr;
        fail(oss.str(), file, line);
    }
}

template <typename A, typename B>
inline void assert_greater_than(const A& threshold, const B& actual,
                                const char* thresholdExpr, const char* actualExpr,
                                const char* file, int line) {
    if (!(actual > threshold)) {
        std::ostringstream oss;
        oss << "expected " << actualExpr << " > " << thresholdExpr;
        fail(oss.str(), file, line);
    }
}

template <typename A, typename B>
inline void assert_greater_or_equal(const A& threshold, const B& actual,
                                    const char* thresholdExpr, const char* actualExpr,
                                    const char* file, int line) {
    if (!(actual >= threshold)) {
        std::ostringstream oss;
        oss << "expected " << actualExpr << " >= " << thresholdExpr;
        fail(oss.str(), file, line);
    }
}

template <typename A, typename B>
inline void assert_less_than(const A& threshold, const B& actual,
                             const char* thresholdExpr, const char* actualExpr,
                             const char* file, int line) {
    if (!(actual < threshold)) {
        std::ostringstream oss;
        oss << "expected " << actualExpr << " < " << thresholdExpr;
        fail(oss.str(), file, line);
    }
}

template <typename A, typename B>
inline void assert_less_or_equal(const A& threshold, const B& actual,
                                 const char* thresholdExpr, const char* actualExpr,
                                 const char* file, int line) {
    if (!(actual <= threshold)) {
        std::ostringstream oss;
        oss << "expected " << actualExpr << " <= " << thresholdExpr;
        fail(oss.str(), file, line);
    }
}

inline void assert_float_within(double delta, double expected, double actual,
                                const char* file, int line) {
    if (std::fabs(expected - actual) > delta) {
        std::ostringstream oss;
        oss << "expected |actual - expected| <= " << delta;
        fail(oss.str(), file, line);
    }
}

inline void assert_memory_equal(const void* expected, const void* actual, size_t len,
                                const char* file, int line) {
    if (std::memcmp(expected, actual, len) != 0) {
        fail("memory blocks differ", file, line);
    }
}

inline void assert_string_equal(const char* expected, const char* actual,
                                const char* file, int line) {
    if (expected == NULL && actual == NULL) return;
    if (expected == NULL || actual == NULL || std::strcmp(expected, actual) != 0) {
        fail("strings differ", file, line);
    }
}

inline int begin() {
    state() = State();
    return 0;
}

inline void run(const char* name, void (*fn)(void)) {
    ++state().tests;
    try {
        setUp();
        fn();
        tearDown();
        std::printf("[PASS] %s\n", name);
    } catch (const std::exception& e) {
        ++state().failures;
        std::printf("[FAIL] %s\n%s\n", name, e.what());
        try { tearDown(); } catch (...) {}
    } catch (...) {
        ++state().failures;
        std::printf("[FAIL] %s\nUnknown exception\n", name);
        try { tearDown(); } catch (...) {}
    }
}

inline int end() {
    std::printf("\n%d tests, %d failures\n", state().tests, state().failures);
    return state().failures == 0 ? 0 : 1;
}

} // namespace tinytest

#define UNITY_BEGIN() tinytest::begin()
#define UNITY_END() tinytest::end()
#define RUN_TEST(fn) tinytest::run(#fn, fn)

#define TEST_ASSERT(condition) \
    do { if (!(condition)) tinytest::fail("assertion failed: " #condition, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT(condition)
#define TEST_ASSERT_FALSE(condition) \
    do { if (condition) tinytest::fail("expected false: " #condition, __FILE__, __LINE__); } while (0)

#define TEST_FAIL_MESSAGE(message) \
    do { tinytest::fail((message), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_NULL(ptr) \
    do { if ((ptr) != NULL) tinytest::fail("expected NULL: " #ptr, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_NOT_NULL(ptr) \
    do { if ((ptr) == NULL) tinytest::fail("expected non-NULL: " #ptr, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL(expected, actual) \
    do { tinytest::assert_equal((expected), (actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_NOT_EQUAL(expected, actual) \
    do { tinytest::assert_not_equal((expected), (actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_UINT8(expected, actual) \
    do { tinytest::assert_equal(static_cast<unsigned int>(expected), static_cast<unsigned int>(actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_UINT16(expected, actual) \
    do { tinytest::assert_equal(static_cast<unsigned int>(expected), static_cast<unsigned int>(actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_UINT32(expected, actual) \
    do { tinytest::assert_equal(static_cast<unsigned long>(expected), static_cast<unsigned long>(actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_UINT64(expected, actual) \
    do { tinytest::assert_equal(static_cast<unsigned long long>(expected), static_cast<unsigned long long>(actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { tinytest::assert_equal(static_cast<long>(expected), static_cast<long>(actual), #expected, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_INT8(expected, actual) TEST_ASSERT_EQUAL(expected, actual)
#define TEST_ASSERT_EQUAL_INT16(expected, actual) TEST_ASSERT_EQUAL(expected, actual)
#define TEST_ASSERT_EQUAL_INT32(expected, actual) TEST_ASSERT_EQUAL(expected, actual)
#define TEST_ASSERT_EQUAL_INT64(expected, actual) TEST_ASSERT_EQUAL(expected, actual)
#define TEST_ASSERT_EQUAL_SIZE_T(expected, actual) TEST_ASSERT_EQUAL(expected, actual)
#define TEST_ASSERT_EQUAL_HEX8(expected, actual) TEST_ASSERT_EQUAL_UINT8(expected, actual)
#define TEST_ASSERT_EQUAL_HEX16(expected, actual) TEST_ASSERT_EQUAL_UINT16(expected, actual)
#define TEST_ASSERT_EQUAL_HEX32(expected, actual) TEST_ASSERT_EQUAL_UINT32(expected, actual)
#define TEST_ASSERT_EQUAL_HEX64(expected, actual) TEST_ASSERT_EQUAL_UINT64(expected, actual)

#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) \
    do { tinytest::assert_float_within((delta), (expected), (actual), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_DOUBLE_WITHIN(delta, expected, actual) \
    do { tinytest::assert_float_within((delta), (expected), (actual), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) \
    do { tinytest::assert_memory_equal((expected), (actual), (len), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    do { tinytest::assert_string_equal((expected), (actual), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_GREATER_THAN(threshold, actual) \
    do { tinytest::assert_greater_than((threshold), (actual), #threshold, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual) \
    do { tinytest::assert_greater_or_equal((threshold), (actual), #threshold, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_LESS_THAN(threshold, actual) \
    do { tinytest::assert_less_than((threshold), (actual), #threshold, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_LESS_OR_EQUAL(threshold, actual) \
    do { tinytest::assert_less_or_equal((threshold), (actual), #threshold, #actual, __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_TRUE_MESSAGE(condition, message) \
    do { if (!(condition)) tinytest::fail((message), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_FALSE_MESSAGE(condition, message) \
    do { if (condition) tinytest::fail((message), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_INT_MESSAGE(expected, actual, message) \
    do { if (!((expected) == (actual))) tinytest::fail((message), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual) \
    do { tinytest::assert_float_within(0.00001f, (expected), (actual), __FILE__, __LINE__); } while (0)

#define TEST_ASSERT_LESS_THAN_UINT32(threshold, actual) \
    do { tinytest::assert_less_than(static_cast<uint32_t>(threshold), static_cast<uint32_t>(actual), #threshold, #actual, __FILE__, __LINE__); } while (0)