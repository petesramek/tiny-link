/**
 * @file adapters/TinyPosixAdapter.h
 * @brief Owning POSIX serial adapter for TinyLink.
 *
 * The adapter opens, configures, owns, and closes one POSIX serial-device file
 * descriptor. Construction leaves the adapter closed when any required setup
 * step fails.
 *
 * POSIX write() results are exposed to TinyLink. Partial writes are not retried
 * by this adapter.
 */
#ifndef TINYLINK_ADAPTERS_TINYPOSIXADAPTER_H
#define TINYLINK_ADAPTERS_TINYPOSIXADAPTER_H

#if defined(_WIN32)
#error "TinyPosixAdapter cannot be compiled on Windows"
#endif

#include <chrono>
#include <stddef.h>
#include <stdint.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "adapters/WriteResult.h"

namespace tinylink {

/**
 * @brief Owning non-blocking serial adapter for POSIX platforms.
 *
 * The constructor opens and configures the device for raw 8N1 communication
 * without hardware or software flow control. Destruction closes the descriptor.
 */
class TinyPosixAdapter {
private:
    int _fileDescriptor;

    TinyPosixAdapter(const TinyPosixAdapter&) = delete;
    TinyPosixAdapter& operator=(const TinyPosixAdapter&) = delete;

    /** @brief Close and invalidate the owned file descriptor. */
    inline void close_port()
    {
        if (_fileDescriptor >= 0) {
            ::close(_fileDescriptor);
            _fileDescriptor = -1;
        }
    }

public:
    /**
     * @brief Open and configure a POSIX serial device.
     *
     * The adapter remains closed when @p port is null, open() fails, the device
     * attributes cannot be read, the supplied speed cannot be applied, or the
     * configured attributes cannot be committed.
     *
     * @param port Null-terminated device path such as "/dev/ttyUSB0".
     * @param baud POSIX termios speed constant such as B9600 or B115200.
     */
    explicit TinyPosixAdapter(
        const char* port,
        speed_t baud = B9600)
        : _fileDescriptor(-1)
    {
        if (!port) {
            return;
        }

        const int descriptor = ::open(
            port,
            O_RDWR | O_NOCTTY | O_NONBLOCK);

        if (descriptor < 0) {
            return;
        }

        _fileDescriptor = descriptor;

        struct termios options;
        if (tcgetattr(_fileDescriptor, &options) != 0) {
            close_port();
            return;
        }

        cfmakeraw(&options);
        options.c_cflag &= ~CSIZE;
        options.c_cflag |= CS8 | CLOCAL | CREAD;
        options.c_cflag &= ~(PARENB | PARODD | CSTOPB);
#ifdef CRTSCTS
        options.c_cflag &= ~CRTSCTS;
#endif
        options.c_iflag &= ~(IXON | IXOFF | IXANY);
        options.c_cc[VMIN] = 0;
        options.c_cc[VTIME] = 0;

        if (cfsetispeed(&options, baud) != 0 ||
            cfsetospeed(&options, baud) != 0 ||
            tcsetattr(_fileDescriptor, TCSANOW, &options) != 0) {
            close_port();
        }
    }

    /** @brief Close the owned serial-device file descriptor. */
    ~TinyPosixAdapter()
    {
        close_port();
    }

    /** @brief Determine whether construction completed successfully. */
    inline bool isOpen() const
    {
        return _fileDescriptor >= 0;
    }

    /**
     * @brief Return the number of bytes currently queued for reading.
     *
     * @return Nonnegative queued-byte count, or zero when closed or on error.
     */
    inline int available() const
    {
        if (!isOpen()) {
            return 0;
        }

        int count = 0;
        if (ioctl(_fileDescriptor, FIONREAD, &count) != 0 || count < 0) {
            return 0;
        }

        return count;
    }

    /**
     * @brief Read one byte without blocking.
     *
     * End-of-file, EAGAIN, EWOULDBLOCK, and other read errors are represented
     * as -1 by the current TinyLink adapter contract.
     *
     * @return Byte in the range 0 through 255, or -1 when no byte was read.
     */
    inline int read()
    {
        if (!isOpen()) {
            return -1;
        }

        uint8_t value = 0;
        const ssize_t bytesRead = ::read(
            _fileDescriptor,
            &value,
            1);

        return bytesRead == 1
            ? static_cast<int>(value)
            : -1;
    }

    /**
     * @brief Write one byte to the serial device.
     *
     * @return Complete, partial, or failed result reported by POSIX.
     *
     * @param value Byte to write.
     */
    inline WriteResult write(uint8_t value)
    {
        if (!isOpen()) {
            return WriteResult::failure();
        }

        const ssize_t bytesWritten = ::write(
            _fileDescriptor,
            &value,
            1);
        return bytesWritten < 0
            ? WriteResult::failure()
            : WriteResult::fromCount(
                1,
                static_cast<size_t>(bytesWritten));
    }

    /**
     * @brief Write a byte sequence to the serial device.
     *
     * Because the descriptor is non-blocking, the returned result may be
     * partial. The adapter does not retry. A null pointer is valid only when
     * @p length is zero.
     *
     * @param data Bytes to write, or nullptr when @p length is zero.
     * @param length Number of bytes to write.
     * @return Outcome and accepted byte count.
     */
    inline WriteResult write(
        const uint8_t* data,
        size_t length)
    {
        if (length == 0) {
            return WriteResult::fromCount(0, 0);
        }

        if (!isOpen() || !data) {
            return WriteResult::failure();
        }

        const ssize_t bytesWritten = ::write(
            _fileDescriptor,
            data,
            length);
        return bytesWritten < 0
            ? WriteResult::failure()
            : WriteResult::fromCount(
                length,
                static_cast<size_t>(bytesWritten));
    }

    /**
     * @brief Return monotonic process-independent time in milliseconds.
     *
     * The value may wrap when unsigned long is 32 bits. TinyLink uses unsigned
     * elapsed-time subtraction, which supports this conventional rollover.
     *
     * @return steady_clock duration since its unspecified epoch in milliseconds.
     */
    inline unsigned long millis() const
    {
        const std::chrono::steady_clock::duration elapsed =
            std::chrono::steady_clock::now().time_since_epoch();

        return static_cast<unsigned long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                elapsed).count());
    }
};

} // namespace tinylink

#endif // TINYLINK_ADAPTERS_TINYPOSIXADAPTER_H
