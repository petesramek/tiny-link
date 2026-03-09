/**
 * @file TinyPosixAdapter.h
 * @brief POSIX-compliant (Linux/macOS) serial adapter for TinyLink.
 */

#ifndef TINY_POSIX_ADAPTER_H
#define TINY_POSIX_ADAPTER_H

#ifndef _WIN32
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <chrono>
#include <stdint.h>

namespace tinylink {

    /**
     * @class TinyPosixAdapter
     * @brief Standard POSIX implementation for Serial communication on Unix-like systems.
     */
    class TinyPosixAdapter {
    private:
        int _fd; /**< File descriptor for the serial port */

    public:
        /**
         * @brief Construct a new POSIX Serial Adapter.
         * @param port Path to the serial device (e.g., "/dev/ttyUSB0").
         * @param baud Baud rate constant (e.g., B9600, B115200).
         */
        TinyPosixAdapter(const char* port, speed_t baud = B9600) {
            _fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
            if (_fd != -1) {
                struct termios opt; 
                tcgetattr(_fd, &opt);
                cfsetispeed(&opt, baud); 
                cfsetospeed(&opt, baud);
                
                // Raw mode: No echo, no canonical processing, no signals
                opt.c_cflag |= (CLOCAL | CREAD | CS8);
                opt.c_lflag &= ~(ICANON | ECHO | ISIG);
                opt.c_iflag &= ~(IXON | IXOFF | IXANY); // Disable flow control
                opt.c_oflag &= ~OPOST;                  // Disable output processing
                
                tcsetattr(_fd, TCSANOW, &opt);
            }
        }

        /** @brief Closes the file descriptor upon destruction. */
        ~TinyPosixAdapter() { 
            if (_fd != -1) ::close(_fd); 
        }

        /** @brief Returns true if the port was successfully opened. */
        inline bool isOpen()   { return _fd != -1; }

        /** @brief Returns number of bytes available in the OS RX queue. */
        inline int available() { 
            int b; 
            return (ioctl(_fd, FIONREAD, &b) < 0) ? 0 : b; 
        }

        /** @brief Reads a single byte. Returns -1 if no data is available. */
        inline int read() { 
            uint8_t b; 
            return (::read(_fd, &b, 1) > 0) ? (int)b : -1; 
        }

        /** @brief Writes a single byte to the serial port. */
        inline void write(uint8_t c) { 
            ::write(_fd, &c, 1); 
        }

        /** @brief Writes a buffer of bytes to the serial port. */
        inline void write(const uint8_t* b, size_t l) { 
            ::write(_fd, b, l); 
        }

        /** @brief Returns system uptime in milliseconds using steady_clock. */
        inline unsigned long millis() {
            return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    };
}

#endif // _WIN32
#endif // TINY_POSIX_ADAPTER_H
