#ifndef TINY_POSIX_ADAPTER_H
#define TINY_POSIX_ADAPTER_H

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <chrono>

class TinyPosixAdapter {
private:
    int _fd;
public:
    TinyPosixAdapter(const char* port, speed_t baud = B9600) {
        _fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
        if (_fd != -1) {
            struct termios opt; tcgetattr(_fd, &opt);
            cfsetispeed(&opt, baud); cfsetospeed(&opt, baud);
            opt.c_cflag |= (CLOCAL | CREAD | CS8);
            opt.c_lflag &= ~(ICANON | ECHO | ISIG);
            tcsetattr(_fd, TCSANOW, &opt);
        }
    }
    ~TinyPosixAdapter() { if (_fd != -1) close(_fd); }
    inline bool isOpen()   { return _fd != -1; }
    inline int available() { int b; return (ioctl(_fd, FIONREAD, &b) < 0) ? 0 : b; }
    inline int read()      { uint8_t b; return (::read(_fd, &b, 1) > 0) ? b : -1; }
    inline void write(uint8_t c) { ::write(_fd, &c, 1); }
    inline void write(const uint8_t* b, size_t l) { ::write(_fd, b, l); }
    inline unsigned long millis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};
#endif
