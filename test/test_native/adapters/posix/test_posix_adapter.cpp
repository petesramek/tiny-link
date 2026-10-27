/**
 * @file test_posix_adapter.cpp
 * @brief POSIX adapter setup, I/O, failure cleanup, and ownership tests.
 *
 * This file is excluded on Windows; syscall wrappers inject deterministic
 * failures while delegating ordinary operations to the host POSIX APIs.
 */

#if !defined(_WIN32)

#define _XOPEN_SOURCE 600

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>
#include <unity.h>
#include <sys/ioctl.h>

namespace posix_adapter_test {

/** Termios setup stage at which a wrapper should inject a one-shot failure. */
enum FailureStage {
    NoFailure,
    FailInputSpeed,
    FailOutputSpeed,
    FailCommit
};

static FailureStage failureStage = NoFailure; /**< Selected setup failure; reset per scenario. */
static int trackedFd = -1; /**< Last descriptor returned by the wrapped open(). */
static bool forceWriteResult = false; /**< When true, bypass the real write syscall. */
static ssize_t forcedWriteResult = 0; /**< Synthetic write return value when forced. */

}

static int posix_test_open(const char* path, int flags);
static int posix_test_close(int fd);
static int posix_test_tcgetattr(int fd, struct termios* settings);
static int posix_test_cfsetispeed(struct termios* settings, speed_t speed);
static int posix_test_cfsetospeed(struct termios* settings, speed_t speed);
static int posix_test_tcsetattr(int fd, int action, const struct termios* settings);
static ssize_t posix_test_write(int fd, const void* buffer, size_t length);

#define open posix_test_open
#define close posix_test_close
#define tcgetattr posix_test_tcgetattr
#define cfsetispeed posix_test_cfsetispeed
#define cfsetospeed posix_test_cfsetospeed
#define tcsetattr posix_test_tcsetattr
#define write posix_test_write
// Redirect only adapter calls to wrappers; each wrapper delegates to the real
// POSIX function unless its test control explicitly requests fault injection.
#include "adapters/TinyPosixAdapter.h"
#undef write
#undef tcsetattr
#undef cfsetospeed
#undef cfsetispeed
#undef tcgetattr
#undef close
#undef open

using tinylink::TinyPosixAdapter;
using tinylink::TinyWriteResult;
using tinylink::TinyWriteStatus;

static_assert(!std::is_copy_constructible<TinyPosixAdapter>::value,
    "POSIX adapters own a descriptor and must not be copied");
static_assert(!std::is_copy_assignable<TinyPosixAdapter>::value,
    "POSIX adapters own a descriptor and must not be copy-assigned");

namespace posix_adapter_test {

/**
 * @brief Restore every syscall override and descriptor observation to baseline.
 *
 * Each failure scenario must opt in again after reset so injected failures do
 * not leak between cases or affect ordinary pseudo-terminal traffic.
 */
static void resetOverrides() {
    failureStage = NoFailure;
    trackedFd = -1;
    forceWriteResult = false;
    forcedWriteResult = 0;
}

/** @brief Checks whether an adapter-owned descriptor has been closed. */
static bool descriptorIsClosed(int fd) {
    errno = 0;
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

/**
 * @class PtyFixture
 * @brief Owns a pseudo-terminal master and exposes its unlocked slave path.
 *
 * The slave path is bounded by the fixed buffer because ptsname() returns a
 * process-owned string; this fixture copies it before subsequent PTY use.
 */
class PtyFixture {
public:
    int master; /**< Owned PTY master descriptor, or -1 when setup failed. */
    char slavePath[128]; /**< Bounded copy of the PTY slave path. */

    PtyFixture() : master(-1), slavePath() {
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (master == -1 || grantpt(master) != 0 || unlockpt(master) != 0) {
            if (master != -1) ::close(master);
            master = -1;
            return;
        }

        const char* path = ptsname(master);
        if (!path || strlen(path) >= sizeof(slavePath)) {
            ::close(master);
            master = -1;
            return;
        }
        strcpy(slavePath, path);
    }

    PtyFixture(const PtyFixture&) = delete;
    PtyFixture& operator=(const PtyFixture&) = delete;

    ~PtyFixture() {
        if (master != -1) ::close(master);
    }

    bool isValid() const { return master != -1; }
};

}

/**
 * @brief Capture the adapter-owned descriptor and otherwise perform real open().
 * @note Wrapping records ownership for cleanup assertions; it does not mock open.
 */
static int posix_test_open(const char* path, int flags) {
    const int fd = ::open(path, flags);
    posix_adapter_test::trackedFd = fd;
    return fd;
}

/** @brief Delegate to the real close() so descriptor-ownership checks stay meaningful. */
static int posix_test_close(int fd) {
    return ::close(fd);
}

/** @brief Delegate to real tcgetattr(); this test suite injects no read failure. */
static int posix_test_tcgetattr(int fd, struct termios* settings) {
    return ::tcgetattr(fd, settings);
}

/** @brief Injects a deterministic input-speed configuration failure when requested. */
static int posix_test_cfsetispeed(struct termios* settings, speed_t speed) {
    if (posix_adapter_test::failureStage == posix_adapter_test::FailInputSpeed) {
        errno = EINVAL;
        return -1;
    }
    return ::cfsetispeed(settings, speed);
}

/** @brief Injects a deterministic output-speed configuration failure when requested. */
static int posix_test_cfsetospeed(struct termios* settings, speed_t speed) {
    if (posix_adapter_test::failureStage == posix_adapter_test::FailOutputSpeed) {
        errno = EINVAL;
        return -1;
    }
    return ::cfsetospeed(settings, speed);
}

/** @brief Injects a deterministic terminal-attribute commit failure when requested. */
static int posix_test_tcsetattr(
    int fd, int action, const struct termios* settings) {
    if (posix_adapter_test::failureStage == posix_adapter_test::FailCommit) {
        errno = EIO;
        return -1;
    }
    return ::tcsetattr(fd, action, settings);
}

/**
 * @brief Return a forced write result or delegate to the real write syscall.
 * @note Forced results model partial/error outcomes that a PTY cannot reliably
 *       produce on demand; resetOverrides() restores real I/O behavior.
 */
static ssize_t posix_test_write(int fd, const void* buffer, size_t length) {
    if (posix_adapter_test::forceWriteResult)
        return posix_adapter_test::forcedWriteResult;
    return ::write(fd, buffer, length);
}

/** @test Null and invalid device paths leave the adapter closed. */
void test_posix_null_and_invalid_paths(void) {
    posix_adapter_test::resetOverrides();
    TinyPosixAdapter nullAdapter(nullptr);
    TEST_ASSERT_FALSE(nullAdapter.isOpen());
    TEST_ASSERT_EQUAL_INT(-1, posix_adapter_test::trackedFd);

    TinyPosixAdapter invalidAdapter(
        "/tmp/tinylink-posix-adapter-path-does-not-exist");
    TEST_ASSERT_FALSE(invalidAdapter.isOpen());
    TEST_ASSERT_EQUAL_INT(-1, posix_adapter_test::trackedFd);
}

/** @test A successful open of a non-terminal fails attribute-read setup and closes. */
void test_posix_attribute_read_failure_closes_descriptor(void) {
    posix_adapter_test::resetOverrides();
    TinyPosixAdapter adapter("/dev/null");
    const int fd = posix_adapter_test::trackedFd;
    const bool opened = fd >= 0;
    const bool closed = posix_adapter_test::descriptorIsClosed(fd);
    TEST_ASSERT_FALSE(adapter.isOpen());
    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(closed);
}

/** @test Input and output speed setup failures both close the opened descriptor. */
void test_posix_speed_configuration_failures_close_descriptor(void) {
    posix_adapter_test::PtyFixture pty;
    TEST_ASSERT_TRUE(pty.isValid());

    const posix_adapter_test::FailureStage stages[] = {
        posix_adapter_test::FailInputSpeed,
        posix_adapter_test::FailOutputSpeed
    };
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
        posix_adapter_test::resetOverrides();
        posix_adapter_test::failureStage = stages[i];
        bool isOpen = false;
        int fd = -1;
        {
            TinyPosixAdapter adapter(pty.slavePath, B9600);
            fd = posix_adapter_test::trackedFd;
            isOpen = adapter.isOpen();
        }
        char diagnostic[112];
        snprintf(diagnostic, sizeof(diagnostic),
            "configuration stage index %u (stage=%d) must fail and release fd",
            static_cast<unsigned>(i), static_cast<int>(stages[i]));
        TEST_ASSERT_FALSE_MESSAGE(isOpen, diagnostic);
        TEST_ASSERT_TRUE_MESSAGE(fd >= 0, diagnostic);
        TEST_ASSERT_TRUE_MESSAGE(posix_adapter_test::descriptorIsClosed(fd),
            diagnostic);
    }
}

/** @test A failed terminal-attribute commit leaves no owned descriptor open. */
void test_posix_attribute_commit_failure_closes_descriptor(void) {
    posix_adapter_test::PtyFixture pty;
    TEST_ASSERT_TRUE(pty.isValid());
    posix_adapter_test::resetOverrides();
    posix_adapter_test::failureStage = posix_adapter_test::FailCommit;

    TinyPosixAdapter adapter(pty.slavePath, B9600);
    const int fd = posix_adapter_test::trackedFd;
    const bool opened = fd >= 0;
    const bool closed = posix_adapter_test::descriptorIsClosed(fd);
    TEST_ASSERT_FALSE(adapter.isOpen());
    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(closed);
}

/** @test PTY setup uses non-blocking raw 8N1 mode and disables software flow control. */
void test_posix_raw_8n1_and_nonblocking_io(void) {
    posix_adapter_test::PtyFixture pty;
    TEST_ASSERT_TRUE(pty.isValid());
    posix_adapter_test::resetOverrides();
    TinyPosixAdapter adapter(pty.slavePath, B115200);
    TEST_ASSERT_TRUE(adapter.isOpen());

    struct termios settings;
    TEST_ASSERT_EQUAL_INT(0, ::tcgetattr(posix_adapter_test::trackedFd, &settings));
    TEST_ASSERT_EQUAL_INT(CS8, settings.c_cflag & CSIZE);
    TEST_ASSERT_EQUAL_INT(0, settings.c_cflag & (PARENB | CSTOPB));
    TEST_ASSERT_TRUE((settings.c_cflag & (CLOCAL | CREAD)) == (CLOCAL | CREAD));
    TEST_ASSERT_EQUAL_INT(0, settings.c_iflag & (IXON | IXOFF | IXANY));
    TEST_ASSERT_EQUAL_INT(0, settings.c_lflag & (ICANON | ECHO | ISIG));
    TEST_ASSERT_EQUAL_INT(0, settings.c_oflag & OPOST);
#ifdef CRTSCTS
    TEST_ASSERT_EQUAL_INT(0, settings.c_cflag & CRTSCTS);
#endif
    TEST_ASSERT_TRUE((fcntl(posix_adapter_test::trackedFd, F_GETFL) & O_NONBLOCK) != 0);

    TEST_ASSERT_EQUAL_INT(0, adapter.available());
    TEST_ASSERT_EQUAL_INT(-1, adapter.read());

    const uint8_t received = 0xa5;
    TEST_ASSERT_EQUAL_INT(1, ::write(pty.master, &received, 1));
    TEST_ASSERT_EQUAL_INT(1, adapter.available());
    TEST_ASSERT_EQUAL_INT(received, adapter.read());

    const uint8_t sent[] = { 0x11, 0x00, 0xff };
    const TinyWriteResult result = adapter.write(sent, sizeof(sent));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Complete),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(sizeof(sent), result.bytesWritten);
    uint8_t output[sizeof(sent)];
    TEST_ASSERT_EQUAL_INT(sizeof(output), ::read(pty.master, output, sizeof(output)));
    TEST_ASSERT_EQUAL_MEMORY(sent, output, sizeof(sent));
}

/** @test POSIX writes report complete, partial, and failed outcomes accurately. */
void test_posix_write_result_contract(void) {
    posix_adapter_test::PtyFixture pty;
    TEST_ASSERT_TRUE(pty.isValid());
    posix_adapter_test::resetOverrides();
    TinyPosixAdapter adapter(pty.slavePath, B9600);
    TEST_ASSERT_TRUE(adapter.isOpen());
    const uint8_t data[] = { 1, 2, 3, 4, 5 };

    posix_adapter_test::forceWriteResult = true;
    posix_adapter_test::forcedWriteResult = 3;
    TinyWriteResult result = adapter.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Partial),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(3, result.bytesWritten);

    posix_adapter_test::forcedWriteResult = -1;
    result = adapter.write(data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TinyWriteStatus::Failed),
        static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);
}

/** @test Destruction releases the adapter-owned descriptor exactly once. */
void test_posix_descriptor_ownership_and_cleanup(void) {
    posix_adapter_test::PtyFixture pty;
    TEST_ASSERT_TRUE(pty.isValid());
    posix_adapter_test::resetOverrides();
    int fd = -1;
    {
        TinyPosixAdapter adapter(pty.slavePath, B9600);
        TEST_ASSERT_TRUE(adapter.isOpen());
        fd = posix_adapter_test::trackedFd;
        TEST_ASSERT_TRUE(fcntl(fd, F_GETFD) != -1);
    }
    TEST_ASSERT_TRUE(posix_adapter_test::descriptorIsClosed(fd));
}

/** @test Adapter time never moves backward when read from the steady clock. */
void test_posix_millis_is_monotonic(void) {
    posix_adapter_test::resetOverrides();
    TinyPosixAdapter adapter(nullptr);
    const unsigned long before = adapter.millis();
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 2000000;
    nanosleep(&delay, nullptr);
    const unsigned long after = adapter.millis();
    const unsigned long later = adapter.millis();
    TEST_ASSERT_TRUE(after >= before);
    TEST_ASSERT_TRUE(later >= after);
}

/** @brief Registers the POSIX adapter tests on Unix-like native test builds. */
void register_posix_adapter_tests(void) {
    RUN_TEST(test_posix_null_and_invalid_paths);
    RUN_TEST(test_posix_attribute_read_failure_closes_descriptor);
    RUN_TEST(test_posix_speed_configuration_failures_close_descriptor);
    RUN_TEST(test_posix_attribute_commit_failure_closes_descriptor);
    RUN_TEST(test_posix_raw_8n1_and_nonblocking_io);
    RUN_TEST(test_posix_write_result_contract);
    RUN_TEST(test_posix_descriptor_ownership_and_cleanup);
    RUN_TEST(test_posix_millis_is_monotonic);
}

#endif
