/**
 * @file protocol/LogLevel.h
 * @brief LogLevel enum for use with TinyLink::sendLog().
 */

#ifndef TINYLINK_LOGLEVEL_H
#define TINYLINK_LOGLEVEL_H

#include <stdint.h>

namespace tinylink {

/**
 * @brief Severity level for log messages sent via TinyLink::sendLog().
 *
 * Values are transmitted as a single byte on the wire inside a Debug ('g') frame.
 * Ordered from least severe (TRACE) to most severe (ERROR).
 */
enum class LogLevel : uint8_t {
    TRACE = 0, /**< Fine-grained trace output (least severe) */
    DEBUG = 1, /**< Verbose diagnostic output */
    INFO  = 2, /**< Informational message */
    WARN  = 3, /**< Warning condition */
    ERROR = 4  /**< Error condition (most severe) */
};

} // namespace tinylink

#endif // TINYLINK_LOGLEVEL_H
