/**
 * @file DebugMessage.h
 * @brief DebugMessage — packed debug/diagnostic payload for MessageType::Debug ('g') frames.
 */

#ifndef TINYLINK_DEBUGMESSAGE_H
#define TINYLINK_DEBUGMESSAGE_H

#include <stdint.h>
#include <string.h>

namespace tinylink {

    /** @brief Maximum number of characters (excluding NUL) in DebugMessage::text. */
    static const uint8_t DEBUG_TEXT_CAPACITY = 27;

#if defined(_MSC_VER)
    #define TINYLINK_DM_PACKED_BEGIN __pragma(pack(push, 1))
    #define TINYLINK_DM_PACKED_END   __pragma(pack(pop))
    #define TINYLINK_DM_PACKED
#else
    #define TINYLINK_DM_PACKED_BEGIN
    #define TINYLINK_DM_PACKED_END
    #define TINYLINK_DM_PACKED __attribute__((packed))
#endif

    /**
     * @brief 32-byte packed debug payload for MessageType::Debug ('g') frames.
     *
     * Layout (32 bytes total):
     *   4 bytes  ts    — timestamp in milliseconds
     *   1 byte   level — severity level (0 = verbose, higher = more severe)
     *  27 bytes  text  — NUL-terminated debug string
     */
    TINYLINK_DM_PACKED_BEGIN
    struct DebugMessage {
        uint32_t ts;                      /**< Timestamp in milliseconds */
        uint8_t  level;                   /**< Severity level */
        char     text[DEBUG_TEXT_CAPACITY]; /**< NUL-terminated debug string */
    } TINYLINK_DM_PACKED;
    TINYLINK_DM_PACKED_END

    static_assert(sizeof(DebugMessage) == 32, "DebugMessage must be exactly 32 bytes");

    /**
     * @brief Copy a C-string into DebugMessage::text, safely NUL-terminating.
     *
     * Copies at most DEBUG_TEXT_CAPACITY - 1 characters and always writes a
     * NUL terminator, so the text field is always a valid C-string.
     *
     * @param msg   DebugMessage to write into.
     * @param text  Source string (may be NULL — results in empty string).
     */
    inline void debugmessage_set_text(DebugMessage& msg, const char* text) {
        if (!text) {
            msg.text[0] = '\0';
            return;
        }
        size_t len = strlen(text);
        if (len >= DEBUG_TEXT_CAPACITY) {
            len = DEBUG_TEXT_CAPACITY - 1;
        }
        memcpy(msg.text, text, len);
        msg.text[len] = '\0';
    }

} // namespace tinylink

#endif // TINYLINK_DEBUGMESSAGE_H
