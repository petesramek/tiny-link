/**
 * @file test_write_result.cpp
 * @brief Unit tests for transport adapter write results.
 */

#include <unity.h>
#include "TinyWriteResult.h"
#include "support/stubs/TinyTestAdapter.h"

using namespace tinylink;

/**
 * @class TinyWriteResultStub
 * @brief Test double that returns configured complete, partial, or failed writes.
 */
class TinyWriteResultStub : public TinyTestAdapter {
private:
    size_t _acceptedBytes; /**< Maximum accepted prefix for each non-empty write. */
    bool _writeSucceeded; /**< Selects complete/partial versus failed status. */

public:
    /**
     * @brief Configure the accepted prefix and reported write outcome.
     * @param acceptedBytes Number of bytes accepted, capped to request length.
     * @param writeSucceeded Whether a non-null request reports success.
     */
    TinyWriteResultStub(size_t acceptedBytes, bool writeSucceeded)
        : _acceptedBytes(acceptedBytes), _writeSucceeded(writeSucceeded) {}

    TinyWriteResult write(const uint8_t* bytes, size_t length) override {
        if (length == 0) return tiny_write_result(0, 0, true);
        const size_t written = bytes
            ? (_acceptedBytes < length ? _acceptedBytes : length)
            : 0;
        return tiny_write_result(length, written, _writeSucceeded && bytes != nullptr);
    }
};

/** @test A successful write of every requested byte is complete. */
void test_write_result_complete(void) {
    const uint8_t data[] = { 1, 2, 3, 4 };
    TinyWriteResultStub adapter(4, true);
    const TinyWriteResult result = adapter.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Complete),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(4, result.bytesWritten);
}

/** @test A write that accepts only some bytes is partial. */
void test_write_result_partial(void) {
    const uint8_t data[] = { 1, 2, 3, 4 };
    TinyWriteResultStub adapter(2, true);
    const TinyWriteResult result = adapter.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Partial),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(2, result.bytesWritten);
}

/** @test A write that accepts no bytes is failed. */
void test_write_result_failed(void) {
    const uint8_t data[] = { 1, 2, 3, 4 };
    TinyWriteResultStub adapter(0, false);
    const TinyWriteResult result = adapter.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Failed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
}

/** @test A zero-byte request is distinct from a failed non-empty write. */
void test_write_result_zero_length(void) {
    TinyWriteResultStub adapter(0, false);
    const TinyWriteResult result = adapter.write(nullptr, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::ZeroLength),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
}

void register_write_result_tests(void) {
    RUN_TEST(test_write_result_complete);
    RUN_TEST(test_write_result_partial);
    RUN_TEST(test_write_result_failed);
    RUN_TEST(test_write_result_zero_length);
}
