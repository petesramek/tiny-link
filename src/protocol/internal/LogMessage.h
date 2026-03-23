/**
 * @file internal/LogMessage.h
 * @brief LogMessage — packed log/diagnostic payload for MessageType::Log ('L') frames.
 *
 * This is the canonical wire format for Log frames produced by TinyLink::sendLog().
 * Receivers can cast the raw payload directly to LogMessage to access all fields.
 *
 * Internal header: not part of the public API surface. Users should not
 * depend on the wire layout defined here.
 */

#ifndef TINYLINK_INTERNAL_LOGMESSAGE_H
#define TINYLINK_INTERNAL_LOGMESSAGE_H

#include <stdint.h>
#include <string.h>
#include "protocol/internal/Packed.h"

namespace tinylink {

    /**
     * @brief Maximum number of characters (excluding NUL) in LogMessage::text.
     *
     * Derived from the 32-byte fixed struct size:
     *   4 (ts) + 1 (level) + 2 (code) + 25 (text) = 32 bytes.
     * The last byte of text is always NUL, so up to 24 printable characters fit.
     */
    static const uint8_t LOG_TEXT_CAPACITY = 25;

    /**
     * @brief 32-byte packed log payload for MessageType::Log ('L') frames.
     *
     * Layout (32 bytes total):
     *   4 bytes  ts    — timestamp in milliseconds (from adapter millis())
     *   1 byte   level — severity level (see LogLevel enum)
     *   2 bytes  code  — application-defined error / event code
     *  25 bytes  text  — NUL-terminated log string (24 printable chars max)
     */
    TINYLINK_PACKED_BEGIN
    struct LogMessage {
        uint32_t ts;                     /**< Timestamp in milliseconds */
        uint8_t  level;                  /**< Severity level (see LogLevel) */
        uint16_t code;                   /**< Application error / event code */
        char     text[LOG_TEXT_CAPACITY]; /**< NUL-terminated log string */
    } TINYLINK_PACKED;
    TINYLINK_PACKED_END

    static_assert(sizeof(LogMessage) == 32, "LogMessage must be exactly 32 bytes");

    /**
     * @brief Copy a C-string into LogMessage::text, safely NUL-terminating.
     *
     * Copies at most LOG_TEXT_CAPACITY - 1 characters and always writes a
     * NUL terminator, so the text field is always a valid C-string.
     *
     * @param msg   LogMessage to write into.
     * @param text  Source string (may be NULL — results in empty string).
     */
    inline void logmessage_set_text(LogMessage& msg, const char* text) {
        if (!text) {
            msg.text[0] = '\0';
            return;
        }
        size_t len = strlen(text);
        if (len >= LOG_TEXT_CAPACITY) {
            len = LOG_TEXT_CAPACITY - 1;
        }
        memcpy(msg.text, text, len);
        msg.text[len] = '\0';
    }

} // namespace tinylink

#endif // TINYLINK_INTERNAL_LOGMESSAGE_H
