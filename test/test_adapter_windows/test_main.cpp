#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <unity.h>

#ifndef _WIN32
#define _WIN32
#endif
#include <adapters/TinyWindowsAdapter.h>
#include <TinyLink.h>

namespace {

    static_assert(
        tinylink::is_valid_adapter<tinylink::TinyWindowsAdapter>::value,
        "Windows adapter must satisfy TinyLink's transport contract");

    void testWindowsAdapterConfiguresOwnsAndClosesSuccessfulHandle()
    {
        {
            tinylink::TinyWindowsAdapter adapter("COM7", 115200u);
            TEST_ASSERT_TRUE(adapter.isOpen());
            TEST_ASSERT_EQUAL_UINT32(115200u, tinylinkWindowsLastState.BaudRate);
            TEST_ASSERT_EQUAL_UINT8(8, tinylinkWindowsLastState.ByteSize);
            TEST_ASSERT_EQUAL_UINT32(ONESTOPBIT, tinylinkWindowsLastState.StopBits);
            TEST_ASSERT_EQUAL_UINT32(NOPARITY, tinylinkWindowsLastState.Parity);
            TEST_ASSERT_EQUAL_UINT32(MAXDWORD,
                tinylinkWindowsLastTimeouts.ReadIntervalTimeout);
            TEST_ASSERT_EQUAL_UINT32(0, tinylinkWindowsCloseCalls);
        }
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsCloseCalls);
    }

    void testWindowsAdapterClosedMethodsAreSafe()
    {
        tinylink::TinyWindowsAdapter nullPort(nullptr);
        TEST_ASSERT_FALSE(nullPort.isOpen());
        TEST_ASSERT_EQUAL_INT(0, nullPort.available());
        TEST_ASSERT_EQUAL_INT(-1, nullPort.read());
        nullPort.write(0xA5u);
        nullPort.write(nullptr, 0);
        TEST_ASSERT_EQUAL_UINT32(0, tinylinkWindowsWriteCalls);

        tinylinkWindowsCreateSucceeds = false;
        tinylink::TinyWindowsAdapter failedOpen("COM8");
        TEST_ASSERT_FALSE(failedOpen.isOpen());
        TEST_ASSERT_EQUAL_INT(0, failedOpen.available());
        TEST_ASSERT_EQUAL_INT(-1, failedOpen.read());
        failedOpen.write(1);
        TEST_ASSERT_EQUAL_UINT32(0, tinylinkWindowsCloseCalls);
        TEST_ASSERT_EQUAL_UINT32(0, tinylinkWindowsWriteCalls);
    }

    void testWindowsAdapterClosesHandleWhenConfigurationFails()
    {
        tinylinkWindowsGetStateSucceeds = false;
        {
            tinylink::TinyWindowsAdapter adapter("COM1");
            TEST_ASSERT_FALSE(adapter.isOpen());
        }
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsCloseCalls);

        tinylink_reset_windows_stub();
        tinylinkWindowsSetStateSucceeds = false;
        {
            tinylink::TinyWindowsAdapter adapter("COM1");
            TEST_ASSERT_FALSE(adapter.isOpen());
        }
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsCloseCalls);

        tinylink_reset_windows_stub();
        tinylinkWindowsSetTimeoutsSucceeds = false;
        {
            tinylink::TinyWindowsAdapter adapter("COM1");
            TEST_ASSERT_FALSE(adapter.isOpen());
        }
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsCloseCalls);

        tinylink_reset_windows_stub();
        tinylinkWindowsPurgeSucceeds = false;
        {
            tinylink::TinyWindowsAdapter adapter("COM1");
            TEST_ASSERT_FALSE(adapter.isOpen());
        }
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsCloseCalls);
    }

    void testWindowsAdapterReportsQueuedBytesAndReadsAvailableByte()
    {
        tinylink::TinyWindowsAdapter adapter("COM2");
        tinylinkWindowsQueuedBytes = 12;
        TEST_ASSERT_EQUAL_INT(12, adapter.available());
        tinylinkWindowsReadValue = 0xE3u;
        TEST_ASSERT_EQUAL_INT(0xE3, adapter.read());
        tinylinkWindowsReadAvailable = false;
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        tinylinkWindowsReadAvailable = true;
        tinylinkWindowsReadSucceeds = false;
        TEST_ASSERT_EQUAL_INT(-1, adapter.read());
        tinylinkWindowsClearErrorSucceeds = false;
        TEST_ASSERT_EQUAL_INT(0, adapter.available());
    }

    void testWindowsAdapterWritesValuesAndEnforcesDwordLengthLimit()
    {
        tinylink::TinyWindowsAdapter adapter("COM3");
        const uint8_t bytes[] = { 0x13u, 0x00u, 0xF1u };
        adapter.write(0xA4u);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsWriteCalls);
        TEST_ASSERT_EQUAL_UINT32(1, tinylinkWindowsLastWriteLength);
        adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_UINT32(2, tinylinkWindowsWriteCalls);
        TEST_ASSERT_EQUAL_UINT32(sizeof(bytes), tinylinkWindowsLastWriteLength);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(
            bytes, tinylinkWindowsWrittenBytes, sizeof(bytes));

        adapter.write(nullptr, 0);
        adapter.write(nullptr, 1);
        TEST_ASSERT_EQUAL_UINT32(2, tinylinkWindowsWriteCalls);

        if (static_cast<size_t>(MAXDWORD) < static_cast<size_t>(-1)) {
            adapter.write(bytes, static_cast<size_t>(MAXDWORD) + 1u);
            TEST_ASSERT_EQUAL_UINT32(2, tinylinkWindowsWriteCalls);
        }

        tinylinkWindowsWriteSucceeds = false;
        adapter.write(bytes, sizeof(bytes));
        TEST_ASSERT_EQUAL_UINT32(3, tinylinkWindowsWriteCalls);
    }

    void testWindowsAdapterUsesDeterministicTickAndSaturatesAvailability()
    {
        tinylink::TinyWindowsAdapter adapter("COM4");
        tinylinkWindowsTick = 0xFEDCBA98u;
        TEST_ASSERT_EQUAL_UINT32(0xFEDCBA98u, adapter.millis());
        tinylinkWindowsQueuedBytes = static_cast<DWORD>(INT_MAX) + 1u;
        TEST_ASSERT_EQUAL_INT(INT_MAX, adapter.available());
    }

} // namespace

int main()
{
    UNITY_BEGIN();
    RUN_TEST(testWindowsAdapterConfiguresOwnsAndClosesSuccessfulHandle);
    RUN_TEST(testWindowsAdapterClosedMethodsAreSafe);
    RUN_TEST(testWindowsAdapterClosesHandleWhenConfigurationFails);
    RUN_TEST(testWindowsAdapterReportsQueuedBytesAndReadsAvailableByte);
    RUN_TEST(testWindowsAdapterWritesValuesAndEnforcesDwordLengthLimit);
    RUN_TEST(testWindowsAdapterUsesDeterministicTickAndSaturatesAvailability);
    return UNITY_END();
}

extern "C" void setUp(void)
{
    tinylink_reset_windows_stub();
}

extern "C" void tearDown(void)
{
}
