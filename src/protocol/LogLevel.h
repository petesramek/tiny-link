/**
 * @file protocol/LogLevel.h
 * @brief Stable log severity levels used by TinyLink.
 *
 * Log levels are one-byte wire identifiers ordered from least severe to most
 * severe. Numeric comparison is a supported contract for threshold filtering.
 * Existing values must never be renumbered. Future levels must use explicit
 * values, preserve severity ordering, and update raw-value validation.
 */
#ifndef TINYLINK_LOGLEVEL_H
#define TINYLINK_LOGLEVEL_H

#include <stdint.h>

namespace tinylink {

    /**
     * @brief Log severity transmitted in TinyLink log payloads.
     *
     * Numeric values increase with severity. Unknown wire values are invalid and
     * must be rejected before they reach callbacks or severity filtering.
     */
    enum class LogLevel : uint8_t {
        TRACE = 0, /**< Detailed execution tracing. */
        DEBUG = 1, /**< Diagnostic information useful during development. */
        INFO = 2, /**< Normal informational event. */
        WARN = 3, /**< Potential problem that does not prevent operation. */
        ERROR = 4  /**< Failure that prevented an intended operation. */
    };

    static_assert(sizeof(LogLevel) == 1,
        "LogLevel must have an 8-bit representation");

    static_assert(static_cast<uint8_t>(LogLevel::TRACE) == 0,
        "LogLevel::TRACE wire value changed");

    static_assert(static_cast<uint8_t>(LogLevel::DEBUG) == 1,
        "LogLevel::DEBUG wire value changed");

    static_assert(static_cast<uint8_t>(LogLevel::INFO) == 2,
        "LogLevel::INFO wire value changed");

    static_assert(static_cast<uint8_t>(LogLevel::WARN) == 3,
        "LogLevel::WARN wire value changed");

    static_assert(static_cast<uint8_t>(LogLevel::ERROR) == 4,
        "LogLevel::ERROR wire value changed");

    /**
     * @brief Determine whether a raw wire byte is a declared log level.
     *
     * @param value Raw log-level byte.
     * @return true for values 0 through 4; otherwise false.
     */
    inline bool is_valid_log_level(uint8_t value)
    {
        switch (value) {
        case static_cast<uint8_t>(LogLevel::TRACE):
        case static_cast<uint8_t>(LogLevel::DEBUG):
        case static_cast<uint8_t>(LogLevel::INFO):
        case static_cast<uint8_t>(LogLevel::WARN):
        case static_cast<uint8_t>(LogLevel::ERROR):
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Determine whether a LogLevel object contains a declared value.
     *
     * @param level Log level to validate.
     * @return true when @p level is declared; otherwise false.
     */
    inline bool is_valid_log_level(LogLevel level)
    {
        return is_valid_log_level(static_cast<uint8_t>(level));
    }

    /**
     * @brief Convert a raw wire byte to a declared LogLevel.
     *
     * The destination remains unchanged when @p value is invalid.
     *
     * @param value Raw log-level byte.
     * @param level Destination updated only when conversion succeeds.
     * @return true when @p value identifies a declared log level.
     */
    inline bool log_level_from_wire(
        uint8_t value,
        LogLevel& level)
    {
        if (!is_valid_log_level(value)) {
            return false;
        }

        level = static_cast<LogLevel>(value);
        return true;
    }

    /**
     * @brief Convert a declared LogLevel to its stable wire byte.
     *
     * Callers must provide a declared value. Raw bytes must be validated with
     * log_level_from_wire() before conversion to LogLevel.
     *
     * @param level Declared log level.
     * @return Stable one-byte wire identifier.
     */
    inline uint8_t log_level_to_wire(LogLevel level)
    {
        return static_cast<uint8_t>(level);
    }

    /**
     * @brief Determine whether a log level meets a minimum severity threshold.
     *
     * @param level Log event severity.
     * @param minimum Minimum severity that should be emitted.
     * @return true when both values are valid and @p level is at least @p minimum.
     */
    inline bool log_level_meets_threshold(
        LogLevel level,
        LogLevel minimum)
    {
        if (!is_valid_log_level(level) ||
            !is_valid_log_level(minimum)) {
            return false;
        }

        return log_level_to_wire(level) >=
            log_level_to_wire(minimum);
    }

    /**
     * @brief Convert a LogLevel to a human-readable diagnostic string.
     *
     * This helper is diagnostic only and does not replace wire-value validation.
     *
     * @param level Log level to describe.
     * @return Static diagnostic string, or "INVALID" for an undeclared value.
     */
    inline const char* log_level_to_string(LogLevel level)
    {
        switch (level) {
        case LogLevel::TRACE:
            return "TRACE";

        case LogLevel::DEBUG:
            return "DEBUG";

        case LogLevel::INFO:
            return "INFO";

        case LogLevel::WARN:
            return "WARN";

        case LogLevel::ERROR:
            return "ERROR";

        default:
            return "INVALID";
        }
    }

} // namespace tinylink

#endif // TINYLINK_LOGLEVEL_H
