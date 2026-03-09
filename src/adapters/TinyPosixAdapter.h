#ifndef TINY_POSIX_ADAPTER_H
#define TINY_POSIX_ADAPTER_H

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <chrono>
#include <stdint.h>

class PosixAdapter {
private:
    int _fd;

public:
    PosixAdapter(const char* portName, speed_t baud = B9600) {
        _fd = open(portName, O_RDWR | O_NOCTTY | O_NDELAY);
        if (_fd != -1) {
            struct termios options;
            tcgetattr(_fd, &options);
            cfsetispeed(&options, baud);
            cfsetospeed(&options, baud);
            options.c_cflag |= (CLOCAL | CREAD | CS8);
            options.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
            options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
            options.c_iflag &= ~(IXON | IXOFF | IXANY);
            options.c_oflag &= ~OPOST;
            tcsetattr(_fd, TCSANOW, &options);
        }
    }

    ~PosixAdapter() { if (_fd != -1) close(_fd); }

    inline bool isOpen() { return _fd != -1; }

    int available() {
        int bytes;
        return (ioctl(_fd, FIONREAD, &bytes) < 0) ? 0 : bytes;
    }

    int read() {
        uint8_t b;
        return (::read(_fd, &b, 1) > 0) ? b : -1;
    }

    void write(uint8_t c) { ::write(_fd, &c, 1); }
    void write(const uint8_t* b, size_t l) { ::write(_fd, b, l); }

    unsigned long millis() {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    }
};

#endif