/**
 * @file internal/protocol/LogMessage.h
 * @brief Internal log payload encoding and decoding for TinyLink.
 *
 * The fixed 32-byte wire payload is encoded field by field and does not depend
 * on compiler structure layout, alignment, packing directives, or host byte
 * order.
 *
 * Internal header: not part of the public API surface.
 */
#ifndef TINYLINK_INTERNAL_LOGMESSAGE_H
#define TINYLINK_INTERNAL_LOGMESSAGE_H

#include <stddef.h>
#include <stdint.h>

#include "protocol/LogLevel.h"

namespace tinylink {

    /** @brief Number of bytes reserved for log text in the wire payload. */
    static const size_t LOG_MESSAGE_TEXT_CAPACITY = 25;

    /** @brief Maximum number of non-NUL characters carried by a log payload. */
    static const size_t LOG_MESSAGE_MAX_TEXT_LENGTH =
        LOG_MESSAGE_TEXT_CAPACITY - 1;

    /** @brief Size of an encoded log payload in bytes. */
    static const size_t LOG_MESSAGE_WIRE_SIZE = 32;

    /**
     * @brief Validated semantic log message.
     *
     * The uptime is the sender's monotonic uptime in milliseconds. The code is an
     * opaque application-defined value. Zero is valid, and TinyLink does not
     * assign meaning to any code range. Text is always NUL-terminated.
     */
    struct LogMessage {
        uint32_t uptimeMilliseconds;          /**< Sender uptime in milliseconds. */
        LogLevel level;                       /**< Validated log severity. */
        uint16_t code;                        /**< Opaque application-defined code. */
        char text[LOG_MESSAGE_TEXT_CAPACITY]; /**< NUL-terminated diagnostic text. */
    };

    /** @brief Result of encoding or decoding a log payload. */
    enum class LogMessageCodecStatus : uint8_t {
        Success,
        InvalidArgument,
        InvalidLength,
        InvalidLevel,
        UnterminatedText,
        OutputTooSmall
    };

    /**
     * @brief Determine whether a log level is declared by the current protocol.
     *
     * @param level Log level to validate.
     * @return true for TRACE through ERROR; otherwise false.
     */
    inline bool is_valid_log_level(LogLevel level)
    {
        switch (level) {
        case LogLevel::TRACE:
        case LogLevel::DEBUG:
        case LogLevel::INFO:
        case LogLevel::WARN:
        case LogLevel::ERROR:
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Convert a raw wire byte to a declared LogLevel.
     *
     * The destination remains unchanged when @p value is not a declared level.
     * This helper is local to the log codec during the initial source pass.
     *
     * @param value Raw log-level byte.
     * @param level Destination updated only when conversion succeeds.
     * @return true when @p value identifies a declared log level.
     */
    inline bool log_level_from_wire(
        uint8_t value,
        LogLevel& level)
    {
        switch (value) {
        case static_cast<uint8_t>(LogLevel::TRACE):
            level = LogLevel::TRACE;
            return true;

        case static_cast<uint8_t>(LogLevel::DEBUG):
            level = LogLevel::DEBUG;
            return true;

        case static_cast<uint8_t>(LogLevel::INFO):
            level = LogLevel::INFO;
            return true;

        case static_cast<uint8_t>(LogLevel::WARN):
            level = LogLevel::WARN;
            return true;

        case static_cast<uint8_t>(LogLevel::ERROR):
            level = LogLevel::ERROR;
            return true;

        default:
            return false;
        }
    }

    /**
     * @brief Set log text from a nullable C string.
     *
     * The complete destination field is initialized. Null input is treated as an
     * empty string. Input longer than LOG_MESSAGE_MAX_TEXT_LENGTH is truncated.
     *
     * @param message Log message whose text field is updated.
     * @param text Nullable NUL-terminated source string.
     */
    inline void set_log_message_text(
        LogMessage& message,
        const char* text)
    {
        size_t index = 0;

        for (; index < LOG_MESSAGE_TEXT_CAPACITY; ++index) {
            message.text[index] = '\0';
        }

        if (!text) {
            return;
        }

        for (index = 0; index < LOG_MESSAGE_MAX_TEXT_LENGTH; ++index) {
            if (text[index] == '\0') {
                break;
            }

            message.text[index] = text[index];
        }
    }

    /**
     * @brief Encode a validated log message into its fixed wire payload.
     *
     * Numeric fields use little-endian byte order. The complete text field is
     * written deterministically and must contain a NUL terminator.
     *
     * @param message Semantic log message to encode.
     * @param output Destination buffer.
     * @param outputCapacity Capacity of @p output in bytes.
     * @return Codec status describing the result.
     */
    inline LogMessageCodecStatus encode_log_message(
        const LogMessage& message,
        uint8_t* output,
        size_t outputCapacity)
    {
        if (!output) {
            return LogMessageCodecStatus::InvalidArgument;
        }

        if (outputCapacity < LOG_MESSAGE_WIRE_SIZE) {
            return LogMessageCodecStatus::OutputTooSmall;
        }

        if (!is_valid_log_level(message.level)) {
            return LogMessageCodecStatus::InvalidLevel;
        }

        bool terminated = false;

        for (size_t index = 0;
            index < LOG_MESSAGE_TEXT_CAPACITY;
            ++index) {
            if (message.text[index] == '\0') {
                terminated = true;
                break;
            }
        }

        if (!terminated) {
            return LogMessageCodecStatus::UnterminatedText;
        }

        uint8_t encoded[LOG_MESSAGE_WIRE_SIZE] = {};

        encoded[0] = static_cast<uint8_t>(
            message.uptimeMilliseconds & 0xFFu);

        encoded[1] = static_cast<uint8_t>(
            (message.uptimeMilliseconds >> 8) & 0xFFu);

        encoded[2] = static_cast<uint8_t>(
            (message.uptimeMilliseconds >> 16) & 0xFFu);

        encoded[3] = static_cast<uint8_t>(
            (message.uptimeMilliseconds >> 24) & 0xFFu);

        encoded[4] = static_cast<uint8_t>(message.level);

        encoded[5] = static_cast<uint8_t>(
            message.code & 0xFFu);

        encoded[6] = static_cast<uint8_t>(
            (message.code >> 8) & 0xFFu);

        bool copying = true;

        for (size_t index = 0;
            index < LOG_MESSAGE_TEXT_CAPACITY;
            ++index) {
            if (copying) {
                encoded[7 + index] =
                    static_cast<uint8_t>(message.text[index]);

                if (message.text[index] == '\0') {
                    copying = false;
                }
            }
        }

        for (size_t index = 0;
            index < LOG_MESSAGE_WIRE_SIZE;
            ++index) {
            output[index] = encoded[index];
        }

        return LogMessageCodecStatus::Success;
    }

    /**
     * @brief Decode and validate a fixed log wire payload.
     *
     * Numeric fields use little-endian byte order. The destination remains
     * unchanged when decoding fails.
     *
     * @param input Encoded log payload.
     * @param inputLength Number of bytes in @p input.
     * @param message Destination updated only after complete validation.
     * @return Codec status describing the result.
     */
    inline LogMessageCodecStatus decode_log_message(
        const uint8_t* input,
        size_t inputLength,
        LogMessage& message)
    {
        if (!input) {
            return LogMessageCodecStatus::InvalidArgument;
        }

        if (inputLength != LOG_MESSAGE_WIRE_SIZE) {
            return LogMessageCodecStatus::InvalidLength;
        }

        LogLevel level = LogLevel::INFO;

        if (!log_level_from_wire(input[4], level)) {
            return LogMessageCodecStatus::InvalidLevel;
        }

        bool terminated = false;

        for (size_t index = 0;
            index < LOG_MESSAGE_TEXT_CAPACITY;
            ++index) {
            if (input[7 + index] == 0) {
                terminated = true;
                break;
            }
        }

        if (!terminated) {
            return LogMessageCodecStatus::UnterminatedText;
        }

        LogMessage decoded = {};

        decoded.uptimeMilliseconds =
            static_cast<uint32_t>(input[0]) |
            (static_cast<uint32_t>(input[1]) << 8) |
            (static_cast<uint32_t>(input[2]) << 16) |
            (static_cast<uint32_t>(input[3]) << 24);

        decoded.level = level;

        decoded.code = static_cast<uint16_t>(
            static_cast<uint16_t>(input[5]) |
            (static_cast<uint16_t>(input[6]) << 8));

        bool copying = true;

        for (size_t index = 0;
            index < LOG_MESSAGE_TEXT_CAPACITY;
            ++index) {
            if (copying) {
                decoded.text[index] =
                    static_cast<char>(input[7 + index]);

                if (input[7 + index] == 0) {
                    copying = false;
                }
            }
        }

        message = decoded;

        return LogMessageCodecStatus::Success;
    }

} // namespace tinylink

#endif // TINYLINK_INTERNAL_LOGMESSAGE_H
