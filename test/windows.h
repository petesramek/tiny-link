#ifndef TINYLINK_TEST_WINDOWS_STUB_H
#define TINYLINK_TEST_WINDOWS_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void* HANDLE;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int BOOL;
typedef void* LPSECURITY_ATTRIBUTES;
typedef void* LPVOID;
typedef const void* LPCVOID;

struct DCB {
    DWORD DCBlength;
    DWORD BaudRate;
    BYTE ByteSize;
    BYTE StopBits;
    BYTE Parity;
    BOOL fBinary;
    BOOL fParity;
    BOOL fOutxCtsFlow;
    BOOL fOutxDsrFlow;
    DWORD fDtrControl;
    BOOL fDsrSensitivity;
    BOOL fTXContinueOnXoff;
    BOOL fOutX;
    BOOL fInX;
    BOOL fErrorChar;
    BOOL fNull;
    DWORD fRtsControl;
    BOOL fAbortOnError;
};

struct COMMTIMEOUTS {
    DWORD ReadIntervalTimeout;
    DWORD ReadTotalTimeoutMultiplier;
    DWORD ReadTotalTimeoutConstant;
    DWORD WriteTotalTimeoutMultiplier;
    DWORD WriteTotalTimeoutConstant;
};

struct COMSTAT {
    DWORD cbInQue;
};

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define OPEN_EXISTING 3u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define CBR_9600 9600u
#define ONESTOPBIT 0u
#define NOPARITY 0u
#define DTR_CONTROL_DISABLE 0u
#define RTS_CONTROL_DISABLE 0u
#define MAXDWORD 0xFFFFFFFFu
#define PURGE_RXABORT 0x0002u
#define PURGE_RXCLEAR 0x0008u
#define PURGE_TXABORT 0x0001u
#define PURGE_TXCLEAR 0x0004u
#define ZeroMemory(destination, length) memset((destination), 0, (length))

static bool tinylinkWindowsCreateSucceeds = true;
static bool tinylinkWindowsGetStateSucceeds = true;
static bool tinylinkWindowsSetStateSucceeds = true;
static bool tinylinkWindowsSetTimeoutsSucceeds = true;
static bool tinylinkWindowsPurgeSucceeds = true;
static bool tinylinkWindowsClearErrorSucceeds = true;
static bool tinylinkWindowsReadSucceeds = true;
static bool tinylinkWindowsReadAvailable = true;
static bool tinylinkWindowsWriteSucceeds = true;
static DWORD tinylinkWindowsQueuedBytes = 0;
static DWORD tinylinkWindowsReadValue = 0;
static DWORD tinylinkWindowsTick = 0;
static size_t tinylinkWindowsCloseCalls = 0;
static size_t tinylinkWindowsWriteCalls = 0;
static DWORD tinylinkWindowsLastWriteLength = 0;
/** @brief Enable a test-selected successful WriteFile byte count. */
static bool tinylinkWindowsWriteCountOverridden = false;
/** @brief Accepted-byte count returned when the Win32 override is enabled. */
static DWORD tinylinkWindowsWriteAcceptedBytes = 0;
static uint8_t tinylinkWindowsWrittenBytes[64] = {};
static DCB tinylinkWindowsLastState = {};
static COMMTIMEOUTS tinylinkWindowsLastTimeouts = {};

inline void tinylink_reset_windows_stub()
{
    tinylinkWindowsCreateSucceeds = true;
    tinylinkWindowsGetStateSucceeds = true;
    tinylinkWindowsSetStateSucceeds = true;
    tinylinkWindowsSetTimeoutsSucceeds = true;
    tinylinkWindowsPurgeSucceeds = true;
    tinylinkWindowsClearErrorSucceeds = true;
    tinylinkWindowsReadSucceeds = true;
    tinylinkWindowsReadAvailable = true;
    tinylinkWindowsWriteSucceeds = true;
    tinylinkWindowsQueuedBytes = 0;
    tinylinkWindowsReadValue = 0;
    tinylinkWindowsTick = 0;
    tinylinkWindowsCloseCalls = 0;
    tinylinkWindowsWriteCalls = 0;
    tinylinkWindowsLastWriteLength = 0;
    tinylinkWindowsWriteCountOverridden = false;
    tinylinkWindowsWriteAcceptedBytes = 0;
    memset(tinylinkWindowsWrittenBytes, 0, sizeof(tinylinkWindowsWrittenBytes));
    memset(&tinylinkWindowsLastState, 0, sizeof(tinylinkWindowsLastState));
    memset(&tinylinkWindowsLastTimeouts, 0, sizeof(tinylinkWindowsLastTimeouts));
}

inline HANDLE CreateFileA(
    const char*,
    DWORD,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD,
    DWORD,
    HANDLE)
{
    return tinylinkWindowsCreateSucceeds
        ? reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1))
        : INVALID_HANDLE_VALUE;
}

inline BOOL CloseHandle(HANDLE)
{
    ++tinylinkWindowsCloseCalls;
    return TRUE;
}

inline BOOL GetCommState(HANDLE, DCB* state)
{
    if (tinylinkWindowsGetStateSucceeds) {
        state->BaudRate = CBR_9600;
    }
    return tinylinkWindowsGetStateSucceeds ? TRUE : FALSE;
}

inline BOOL SetCommState(HANDLE, const DCB* state)
{
    tinylinkWindowsLastState = *state;
    return tinylinkWindowsSetStateSucceeds ? TRUE : FALSE;
}

inline BOOL SetCommTimeouts(HANDLE, const COMMTIMEOUTS* timeouts)
{
    tinylinkWindowsLastTimeouts = *timeouts;
    return tinylinkWindowsSetTimeoutsSucceeds ? TRUE : FALSE;
}

inline BOOL PurgeComm(HANDLE, DWORD)
{
    return tinylinkWindowsPurgeSucceeds ? TRUE : FALSE;
}

inline BOOL ClearCommError(HANDLE, DWORD* errors, COMSTAT* status)
{
    if (!tinylinkWindowsClearErrorSucceeds) {
        return FALSE;
    }
    *errors = 0;
    status->cbInQue = tinylinkWindowsQueuedBytes;
    return TRUE;
}

inline BOOL ReadFile(
    HANDLE,
    LPVOID buffer,
    DWORD length,
    DWORD* bytesRead,
    LPVOID)
{
    if (!tinylinkWindowsReadSucceeds || !tinylinkWindowsReadAvailable ||
        length == 0) {
        *bytesRead = 0;
        return tinylinkWindowsReadSucceeds ? TRUE : FALSE;
    }
    *static_cast<uint8_t*>(buffer) = static_cast<uint8_t>(tinylinkWindowsReadValue);
    *bytesRead = 1;
    return TRUE;
}

inline BOOL WriteFile(
    HANDLE,
    LPCVOID buffer,
    DWORD length,
    DWORD* bytesWritten,
    LPVOID)
{
    ++tinylinkWindowsWriteCalls;
    tinylinkWindowsLastWriteLength = length;
    const uint8_t* bytes = static_cast<const uint8_t*>(buffer);
    if (bytes && length <= sizeof(tinylinkWindowsWrittenBytes)) {
        for (DWORD i = 0; i < length; ++i) {
            tinylinkWindowsWrittenBytes[i] = bytes[i];
        }
    }
    *bytesWritten = tinylinkWindowsWriteSucceeds
        ? (tinylinkWindowsWriteCountOverridden
            ? tinylinkWindowsWriteAcceptedBytes
            : length)
        : 0;
    return tinylinkWindowsWriteSucceeds ? TRUE : FALSE;
}

inline DWORD GetTickCount()
{
    return tinylinkWindowsTick;
}

#endif // TINYLINK_TEST_WINDOWS_STUB_H
