/**
 * @file adapters/WriteResult.h
 * @brief Result of one TinyLink adapter write operation.
 */
#ifndef TINYLINK_ADAPTERS_WRITERESULT_H
#define TINYLINK_ADAPTERS_WRITERESULT_H

#include <stddef.h>
#include <stdint.h>

namespace tinylink {

/**
 * @brief Outcome category for a transport write request.
 */
enum class WriteStatus : uint8_t {
    Complete,  /**< Every requested byte was accepted. */
    Partial,   /**< Some, but not all, requested bytes were accepted. */
    Failed,    /**< A nonempty write failed or accepted no bytes. */
    ZeroLength /**< The request contained no bytes and performed no I/O. */
};

/**
 * @brief Result of one adapter write, including the accepted byte count.
 *
 * A zero-length request is distinct from a failed nonempty write. Adapters do
 * not retry partial writes; callers must treat them as incomplete.
 */
struct WriteResult {
    WriteStatus status; /**< Outcome category for the request. */
    size_t bytesWritten; /**< Number of bytes reported as accepted. */

    /**
     * @brief Create the outcome for a platform-reported byte count.
     *
     * @param requested Number of bytes requested.
     * @param written Number of bytes the platform reports as accepted.
     * @return Corresponding complete, partial, failed, or zero-length result.
     */
    static WriteResult fromCount(size_t requested, size_t written)
    {
        if (requested == 0) {
            return { WriteStatus::ZeroLength, 0 };
        }

        if (written == requested) {
            return { WriteStatus::Complete, written };
        }

        if (written == 0 || written > requested) {
            return { WriteStatus::Failed, 0 };
        }

        return { WriteStatus::Partial, written };
    }

    /**
     * @brief Create a failed result when the platform reports an error.
     *
     * @return Failed result with no accepted bytes.
     */
    static WriteResult failure()
    {
        return { WriteStatus::Failed, 0 };
    }

    /**
     * @brief Determine whether all requested bytes were accepted.
     *
     * @return true only for a complete nonempty write.
     */
    bool complete() const
    {
        return status == WriteStatus::Complete;
    }
};

} // namespace tinylink

#endif // TINYLINK_ADAPTERS_WRITERESULT_H
