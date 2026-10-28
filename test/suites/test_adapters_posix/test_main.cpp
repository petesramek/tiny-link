/**
 * Behavioral tests for TinyPosixAdapter using Linux pseudo-terminals (PTYs).
 *
 * The adapter under test is the production header. No serial hardware and no
 * syscall overrides are used. On Windows the suite compiles to a single ignored
 * test because POSIX PTYs are unavailable; on POSIX hosts every test must pass.
 */
#include <unity.h>

#if defined(_WIN32)

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace {
    void testPosixAdapterRequiresPosixHost()
    {
        TEST_IGNORE_MESSAGE("POSIX PTY tests run only on POSIX hosts");
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testPosixAdapterRequiresPosixHost);
    return UNITY_END();
}

#else

#include <chrono>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <vector>

#include "support/tinylink/TestDiagnostics.h"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <adapters/TinyPosixAdapter.h>
#include <TinyLink.h>

namespace {

    using tinylink::TinyPosixAdapter;
    using tinylink::WriteResult;
    using tinylink::WriteStatus;

    static_assert(
        tinylink::is_valid_adapter<TinyPosixAdapter>::value,
        "POSIX adapter must satisfy TinyLink's transport contract");
    static_assert(
        !std::is_copy_constructible<TinyPosixAdapter>::value,
        "POSIX adapter owns a descriptor and must not be copy constructible");
    static_assert(
        !std::is_copy_assignable<TinyPosixAdapter>::value,
        "POSIX adapter owns a descriptor and must not be copy assignable");

    int masterFd = -1;
    char slavePath[256];

    int statusOf(const WriteResult& result)
    {
        return static_cast<int>(result.status);
    }

    /** Lowest descriptor number the next successful open() would receive. */
    int lowestFreeDescriptor()
    {
        const int probe = ::open("/dev/null", O_RDONLY);
        if (probe >= 0) {
            ::close(probe);
        }
        return probe;
    }

    bool descriptorIsOpen(int descriptor)
    {
        return ::fcntl(descriptor, F_GETFD) != -1;
    }

    /** Wait until the adapter reports at least @p count queued bytes. */
    bool waitForAvailable(const TinyPosixAdapter& adapter, int count)
    {
        for (int i = 0; i < 2000; ++i) {
            if (adapter.available() >= count) {
                return true;
            }
            ::usleep(1000);
        }
        return false;
    }

    /** Read up to @p count bytes from the PTY master, waiting briefly. */
    std::vector<uint8_t> readMaster(size_t count)
    {
        std::vector<uint8_t> bytes;
        for (int i = 0; i < 3000 && bytes.size() < count; ++i) {
            uint8_t buffer[4096];
            const size_t want = count - bytes.size() < sizeof(buffer)
                ? count - bytes.size()
                : sizeof(buffer);
            const ssize_t got = ::read(masterFd, buffer, want);
            if (got > 0) {
                bytes.insert(bytes.end(), buffer, buffer + got);
            } else {
                ::usleep(1000);
            }
        }
        return bytes;
    }

    void assertMasterEmpty()
    {
        ::usleep(20000);
        uint8_t sink = 0;
        TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(::read(masterFd, &sink, 1)));
        TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
    }

    void openPty()
    {
        masterFd = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        TEST_ASSERT_TRUE(masterFd >= 0);
        TEST_ASSERT_EQUAL_INT(0, ::grantpt(masterFd));
        TEST_ASSERT_EQUAL_INT(0, ::unlockpt(masterFd));
        const char* name = ::ptsname(masterFd);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(strlen(name) < sizeof(slavePath));
        strcpy(slavePath, name);
    }

    void closeMaster()
    {
        if (masterFd >= 0) {
            ::close(masterFd);
            masterFd = -1;
        }
    }

    void testInvalidPathsLeaveAdapterClosedAndLeakNothing()
    {
        const int before = lowestFreeDescriptor();
        const char* const paths[] = {
            nullptr,
            "",
            "/tinylink/does/not/exist",
            "/", // open(O_RDWR) on a directory fails
        };
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
            TinyPosixAdapter adapter(paths[i]);
            const char* context = tinylink_test::diag(
                "unopenable path index=%u path=%s",
                (unsigned)i, paths[i] ? paths[i] : "(null)");
            TEST_ASSERT_FALSE_MESSAGE(adapter.isOpen(), context);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, adapter.available(), context);
            TEST_ASSERT_EQUAL_INT_MESSAGE(-1, adapter.read(), context);
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                WriteStatus::Failed, statusOf(adapter.write(0x41)), context);
            TEST_ASSERT_EQUAL_INT_MESSAGE(before, lowestFreeDescriptor(), context);
        }
    }

    void testClosedAdapterWritesReportFailureOrZeroLength()
    {
        TinyPosixAdapter adapter(nullptr);
        const uint8_t data[2] = { 1, 2 };

        const WriteResult one = adapter.write(0x55);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(one));
        TEST_ASSERT_EQUAL_UINT32(0, one.bytesWritten);

        const WriteResult many = adapter.write(data, sizeof(data));
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(many));
        TEST_ASSERT_EQUAL_UINT32(0, many.bytesWritten);

        const WriteResult empty = adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL_INT(WriteStatus::ZeroLength, statusOf(empty));
        TEST_ASSERT_EQUAL_UINT32(0, empty.bytesWritten);
    }

    void testNonTerminalDeviceFailsConfigurationAndClosesDescriptor()
    {
        const int before = lowestFreeDescriptor();
        {
            // /dev/null opens successfully but tcgetattr() fails with ENOTTY.
            TinyPosixAdapter adapter("/dev/null");
            TEST_ASSERT_FALSE(adapter.isOpen());
            TEST_ASSERT_EQUAL_INT(before, lowestFreeDescriptor());
        }
        TEST_ASSERT_EQUAL_INT(before, lowestFreeDescriptor());
    }

    void testUnsupportedSpeedFailsConfigurationAndClosesDescriptor()
    {
        openPty();
        const int before = lowestFreeDescriptor();
        {
            TinyPosixAdapter adapter(slavePath, static_cast<speed_t>(0x7FFFFFF0));
            TEST_ASSERT_FALSE(adapter.isOpen());
            TEST_ASSERT_EQUAL_INT(before, lowestFreeDescriptor());
            TEST_ASSERT_EQUAL_INT(0, adapter.available());
            TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        }
        TEST_ASSERT_EQUAL_INT(before, lowestFreeDescriptor());
    }

    void testOpenConfiguresRaw8N1WithoutFlowControlAndNonblocking()
    {
        openPty();
        const int fd = lowestFreeDescriptor();
        TinyPosixAdapter adapter(slavePath, B115200);
        TEST_ASSERT_TRUE(adapter.isOpen());

        // The adapter took the lowest free descriptor; inspect that open file.
        TEST_ASSERT_TRUE(descriptorIsOpen(fd));
        const int flags = ::fcntl(fd, F_GETFL);
        TEST_ASSERT_TRUE(flags != -1);
        TEST_ASSERT_TRUE((flags & O_NONBLOCK) != 0);
        TEST_ASSERT_EQUAL_INT(O_RDWR, flags & O_ACCMODE);

        struct termios options;
        TEST_ASSERT_EQUAL_INT(0, ::tcgetattr(fd, &options));
        TEST_ASSERT_EQUAL_UINT(CS8, options.c_cflag & CSIZE);
        TEST_ASSERT_EQUAL_UINT(0, options.c_cflag & PARENB);
        TEST_ASSERT_EQUAL_UINT(0, options.c_cflag & PARODD);
        TEST_ASSERT_EQUAL_UINT(0, options.c_cflag & CSTOPB);
        TEST_ASSERT_TRUE((options.c_cflag & CREAD) != 0);
        TEST_ASSERT_TRUE((options.c_cflag & CLOCAL) != 0);
#ifdef CRTSCTS
        TEST_ASSERT_EQUAL_UINT(0, options.c_cflag & CRTSCTS);
#endif
        TEST_ASSERT_EQUAL_UINT(0, options.c_iflag & (IXON | IXOFF | IXANY));
        TEST_ASSERT_EQUAL_UINT(0, options.c_lflag & (ICANON | ECHO | ISIG | IEXTEN));
        TEST_ASSERT_EQUAL_UINT(0, options.c_oflag & OPOST);
        TEST_ASSERT_EQUAL_UINT8(0, options.c_cc[VMIN]);
        TEST_ASSERT_EQUAL_UINT8(0, options.c_cc[VTIME]);
        TEST_ASSERT_EQUAL_UINT(B115200, cfgetispeed(&options));
        TEST_ASSERT_EQUAL_UINT(B115200, cfgetospeed(&options));
    }

    void testDefaultSpeedIs9600()
    {
        openPty();
        const int fd = lowestFreeDescriptor();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());
        struct termios options;
        TEST_ASSERT_EQUAL_INT(0, ::tcgetattr(fd, &options));
        TEST_ASSERT_EQUAL_UINT(B9600, cfgetispeed(&options));
        TEST_ASSERT_EQUAL_UINT(B9600, cfgetospeed(&options));
    }

    void testDestructorClosesOwnedDescriptor()
    {
        openPty();
        const int fd = lowestFreeDescriptor();
        {
            TinyPosixAdapter adapter(slavePath);
            TEST_ASSERT_TRUE(adapter.isOpen());
            TEST_ASSERT_TRUE(descriptorIsOpen(fd));
        }
        TEST_ASSERT_FALSE(descriptorIsOpen(fd));
        TEST_ASSERT_EQUAL_INT(fd, lowestFreeDescriptor());
    }

    void testReadsReturnQueuedBytesInOrderWithoutBlocking()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());

        // Bytes that cooked mode would translate or interpret must pass raw.
        const uint8_t input[] = { 0x00, 0x0A, 0x0D, 0x03, 0x11, 0x13, 0x7F, 0xFF };
        TEST_ASSERT_EQUAL_INT(
            static_cast<int>(sizeof(input)),
            static_cast<int>(::write(masterFd, input, sizeof(input))));
        TEST_ASSERT_TRUE(waitForAvailable(adapter, static_cast<int>(sizeof(input))));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(sizeof(input)), adapter.available());

        for (size_t i = 0; i < sizeof(input); ++i) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                input[i], adapter.read(),
                tinylink_test::diag("byte index=%u of %u",
                    (unsigned)i, (unsigned)sizeof(input)));
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                static_cast<int>(sizeof(input) - i - 1), adapter.available(),
                tinylink_test::diag("available() after reading byte index=%u of %u",
                    (unsigned)i, (unsigned)sizeof(input)));
        }
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
    }

    void testReadReportsNoByteAfterPeerCloses()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());
        closeMaster();
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
        TEST_ASSERT_TRUE(adapter.isOpen());
    }

    void testSingleByteWriteIsCompleteAndReachesPeer()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        const WriteResult result = adapter.write(0xA5);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Complete, statusOf(result));
        TEST_ASSERT_EQUAL_UINT32(1, result.bytesWritten);
        TEST_ASSERT_TRUE(result.complete());

        const std::vector<uint8_t> received = readMaster(1);
        TEST_ASSERT_EQUAL_UINT32(1, received.size());
        TEST_ASSERT_EQUAL_UINT8(0xA5, received[0]);
    }

    void testBufferWriteIsCompleteAndPreservesRawBytes()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        const uint8_t output[] = { 0x00, 0x0A, 0x0D, 0x7E, 0x80, 0xFF };
        const WriteResult result = adapter.write(output, sizeof(output));
        TEST_ASSERT_EQUAL_INT(WriteStatus::Complete, statusOf(result));
        TEST_ASSERT_EQUAL_UINT32(sizeof(output), result.bytesWritten);

        const std::vector<uint8_t> received = readMaster(sizeof(output));
        TEST_ASSERT_EQUAL_UINT32(sizeof(output), received.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(output, &received[0], sizeof(output));
    }

    void testZeroLengthWriteDoesNoIo()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());
        const uint8_t byte = 0x42;

        const WriteResult nullEmpty = adapter.write(nullptr, 0);
        TEST_ASSERT_EQUAL_INT(WriteStatus::ZeroLength, statusOf(nullEmpty));
        TEST_ASSERT_EQUAL_UINT32(0, nullEmpty.bytesWritten);
        TEST_ASSERT_FALSE(nullEmpty.complete());

        const WriteResult validEmpty = adapter.write(&byte, 0);
        TEST_ASSERT_EQUAL_INT(WriteStatus::ZeroLength, statusOf(validEmpty));
        TEST_ASSERT_EQUAL_UINT32(0, validEmpty.bytesWritten);

        assertMasterEmpty();
    }

    void testNonemptyNullBufferFailsWithoutIo()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        const WriteResult result = adapter.write(nullptr, 4);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(result));
        TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);

        assertMasterEmpty();
    }

    void testOversizedNonblockingWriteIsPartialAndNotRetried()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        const size_t requested = 4u * 1024u * 1024u;
        std::vector<uint8_t> payload(requested);
        for (size_t i = 0; i < requested; ++i) {
            payload[i] = static_cast<uint8_t>(i * 131u + 7u);
        }

        const WriteResult result = adapter.write(&payload[0], requested);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Partial, statusOf(result));
        TEST_ASSERT_TRUE(result.bytesWritten > 0);
        TEST_ASSERT_TRUE(result.bytesWritten < requested);
        TEST_ASSERT_FALSE(result.complete());

        // Exactly the accepted prefix is delivered; the rest is not retried.
        const std::vector<uint8_t> received = readMaster(result.bytesWritten);
        TEST_ASSERT_EQUAL_UINT32(result.bytesWritten, received.size());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            &payload[0], &received[0], result.bytesWritten);
        assertMasterEmpty();
    }

    void testWriteToFullQueueFailsInsteadOfBlocking()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());

        std::vector<uint8_t> chunk(65536, 0x5A);
        size_t accepted = 0;
        WriteResult result = WriteResult::failure();
        for (int i = 0; i < 1024; ++i) {
            result = adapter.write(&chunk[0], chunk.size());
            accepted += result.bytesWritten;
            if (result.status == WriteStatus::Failed) {
                break;
            }
        }
        TEST_ASSERT_TRUE(accepted > 0);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(result));
        TEST_ASSERT_EQUAL_UINT32(0, result.bytesWritten);

        // The kernel may release a little buffer space asynchronously, so
        // keep filling with single bytes until one is rejected.
        WriteResult single = adapter.write(0x01);
        for (int i = 0; i < 1000000 && single.status != WriteStatus::Failed; ++i) {
            single = adapter.write(0x01);
        }
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(single));
        TEST_ASSERT_EQUAL_UINT32(0, single.bytesWritten);
    }

    void testWriteAfterPeerClosesFails()
    {
        openPty();
        TinyPosixAdapter adapter(slavePath);
        TEST_ASSERT_TRUE(adapter.isOpen());
        closeMaster();

        const WriteResult single = adapter.write(0x01);
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(single));
        TEST_ASSERT_EQUAL_UINT32(0, single.bytesWritten);

        const uint8_t data[3] = { 1, 2, 3 };
        const WriteResult many = adapter.write(data, sizeof(data));
        TEST_ASSERT_EQUAL_INT(WriteStatus::Failed, statusOf(many));
        TEST_ASSERT_EQUAL_UINT32(0, many.bytesWritten);
    }

    void testMillisIsMonotonicAndTracksElapsedTime()
    {
        TinyPosixAdapter adapter(nullptr);
        const auto start = std::chrono::steady_clock::now();
        const unsigned long first = adapter.millis();
        unsigned long previous = first;
        for (int i = 0; i < 5; ++i) {
            ::usleep(10000);
            const unsigned long now = adapter.millis();
            // Unsigned subtraction is nonnegative and small unless time ran backwards.
            TEST_ASSERT_TRUE(static_cast<unsigned long>(now - previous) < 1000ul);
            previous = now;
        }
        const unsigned long elapsed = static_cast<unsigned long>(previous - first);
        const long long reference =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
        TEST_ASSERT_TRUE(elapsed >= 40ul);
        TEST_ASSERT_TRUE(static_cast<long long>(elapsed) <= reference + 1);
    }

} // namespace

extern "C" void setUp(void)
{
    masterFd = -1;
    slavePath[0] = '\0';
}

extern "C" void tearDown(void)
{
    closeMaster();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testInvalidPathsLeaveAdapterClosedAndLeakNothing);
    RUN_TEST(testClosedAdapterWritesReportFailureOrZeroLength);
    RUN_TEST(testNonTerminalDeviceFailsConfigurationAndClosesDescriptor);
    RUN_TEST(testUnsupportedSpeedFailsConfigurationAndClosesDescriptor);
    RUN_TEST(testOpenConfiguresRaw8N1WithoutFlowControlAndNonblocking);
    RUN_TEST(testDefaultSpeedIs9600);
    RUN_TEST(testDestructorClosesOwnedDescriptor);
    RUN_TEST(testReadsReturnQueuedBytesInOrderWithoutBlocking);
    RUN_TEST(testReadReportsNoByteAfterPeerCloses);
    RUN_TEST(testSingleByteWriteIsCompleteAndReachesPeer);
    RUN_TEST(testBufferWriteIsCompleteAndPreservesRawBytes);
    RUN_TEST(testZeroLengthWriteDoesNoIo);
    RUN_TEST(testNonemptyNullBufferFailsWithoutIo);
    RUN_TEST(testOversizedNonblockingWriteIsPartialAndNotRetried);
    RUN_TEST(testWriteToFullQueueFailsInsteadOfBlocking);
    RUN_TEST(testWriteAfterPeerClosesFails);
    RUN_TEST(testMillisIsMonotonicAndTracksElapsedTime);
    return UNITY_END();
}

#endif
