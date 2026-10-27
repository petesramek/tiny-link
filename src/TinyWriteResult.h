/**
 * @file TinyWriteResult.h
 * @brief Result model shared by TinyLink transport adapters.
 */

#ifndef TINY_WRITE_RESULT_H
#define TINY_WRITE_RESULT_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {

    /**
     * @brief Outcome of one adapter write request.
     */
    enum class TinyWriteStatus : uint8_t {
        Complete,   /**< Every requested byte was accepted. */
        Partial,    /**< Some, but not all, requested bytes were accepted. */
        Failed,     /**< No complete or partial write was reported. */
        ZeroLength  /**< The caller requested zero bytes; no platform write was made. */
    };

    /**
     * @brief Allocation-free result returned by adapter write operations.
     *
     * bytesWritten reports the count accepted by the underlying transport when
     * available. A zero-length request has ZeroLength status and writes no bytes.
     */
    struct TinyWriteResult {
        TinyWriteStatus status;
        size_t bytesWritten;
    };

    /**
     * @brief Classifies the underlying write result against the request size.
     */
    inline TinyWriteResult tiny_write_result(
        size_t requested,
        size_t written,
        bool writeSucceeded
    ) {
        TinyWriteResult result;
        result.bytesWritten = written;

        if (requested == 0) {
            result.status = TinyWriteStatus::ZeroLength;
            result.bytesWritten = 0;
        } else if (writeSucceeded && written == requested) {
            result.status = TinyWriteStatus::Complete;
        } else if (written > 0 && written < requested) {
            result.status = TinyWriteStatus::Partial;
        } else {
            result.status = TinyWriteStatus::Failed;
        }

        return result;
    }

} // namespace tinylink

#endif // TINY_WRITE_RESULT_H
