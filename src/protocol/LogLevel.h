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
 */
enum class LogLevel : uint8_t {
    DEBUG = 0, /**< Verbose diagnostic output */
    INFO  = 1, /**< Informational message */
    WARN  = 2, /**< Warning condition */
    ERROR = 3, /**< Error condition */
    TRACE = 4  /**< Fine-grained trace output */
};

} // namespace tinylink

#endif // TINYLINK_LOGLEVEL_H
