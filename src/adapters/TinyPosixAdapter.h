/**
 * @file TinyPosixAdapter.h
 * @brief POSIX-compliant (Linux/macOS) serial adapter for TinyLink.
 *
 * This adapter uses termios and standard POSIX APIs to provide a
 * TinyLink-compatible serial transport on Unix-like systems.
 */

#ifndef TINY_POSIX_ADAPTER_H
#define TINY_POSIX_ADAPTER_H

#if !defined(_WIN32)
 // POSIX implementation
#else
#error "TinyPosixAdapter cannot be compiled on Windows."
#endif

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <chrono>
#include <stdint.h>
#include <stddef.h>

namespace tinylink {

    /**
     * @class TinyPosixAdapter
     * @brief Standard POSIX implementation for serial communication on Unix-like systems.
     *
     * Adapter contract (required by TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - void          write(uint8_t);
     *   - void          write(const uint8_t*, size_t);
     *   - unsigned long millis();
     */
    class TinyPosixAdapter {
    private:
        int _fd; /**< File descriptor for the serial port (-1 if not open) */

    public:
        /**
         * @brief Construct a new TinyPosixAdapter.
         * @param port Path to the serial device (e.g., "/dev/ttyUSB0").
         * @param baud Baud rate constant (e.g., B9600, B115200).
         */
        explicit TinyPosixAdapter(const char* port, speed_t baud = B9600)
            : _fd(-1) {
            if (!port) return;

            _fd = ::open(port, O_RDWR | O_NOCTTY | O_NDELAY);
            if (_fd != -1) {
                struct termios opt;
                if (tcgetattr(_fd, &opt) == 0) {
                    cfsetispeed(&opt, baud);
                    cfsetospeed(&opt, baud);

                    // Raw mode: no echo, no canonical processing, no signals
                    opt.c_cflag |= (CLOCAL | CREAD | CS8);
                    opt.c_lflag &= ~(ICANON | ECHO | ISIG);
                    opt.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable SW flow control
                    opt.c_oflag &= ~OPOST;                  // Disable output processing

                    tcsetattr(_fd, TCSANOW, &opt);
                }
            }
        }

        /**
         * @brief Closes the file descriptor upon destruction.
         */
        ~TinyPosixAdapter() {
            if (_fd != -1) {
                ::close(_fd);
                _fd = -1;
            }
        }

        /**
         * @brief Returns true if the port was successfully opened.
         */
        inline bool isOpen() {
            return _fd != -1;
        }

        /**
         * @brief Returns number of bytes available in the OS RX queue.
         */
        inline int available() {
            if (_fd == -1) return 0;
            int b = 0;
            return (ioctl(_fd, FIONREAD, &b) < 0) ? 0 : b;
        }

        /**
         * @brief Reads a single byte. Non-blocking.
         * @return Byte in range [0,255], or -1 if no data is available or on error.
         */
        inline int read() {
            if (_fd == -1) return -1;
            uint8_t b = 0;
            return (::read(_fd, &b, 1) > 0) ? static_cast<int>(b) : -1;
        }

        /**
         * @brief Writes a single byte to the serial port.
         */
        inline void write(uint8_t c) {
            if (_fd == -1) return;
            ::write(_fd, &c, 1);
        }

        /**
         * @brief Writes a buffer of bytes to the serial port.
         */
        inline void write(const uint8_t* b, size_t l) {
            if (_fd == -1 || !b || l == 0) return;
            ::write(_fd, b, l);
        }

        /**
         * @brief Returns system uptime in milliseconds using steady_clock.
         */
        inline unsigned long millis() {
            return static_cast<unsigned long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
                );
        }
    };

} // namespace tinylink

#endif // _WIN32
#endif // TINY_POSIX_ADAPTER_H
