/**
 * @file adapters/TinyWindowsAdapter.h
 * @brief Owning Win32 serial adapter for TinyLink.
 *
 * The adapter opens, configures, owns, and closes one Windows serial handle.
 * Construction leaves the adapter closed when any required setup step fails.
 *
 * Write methods return TinyWriteResult using the accepted byte count and status
 * reported by WriteFile().
 */
#ifndef TINYLINK_ADAPTERS_TINYWINDOWSADAPTER_H
#define TINYLINK_ADAPTERS_TINYWINDOWSADAPTER_H

#ifdef _WIN32

#include <windows.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "../TinyWriteResult.h"

namespace tinylink {

    /**
     * @brief Owning synchronous serial adapter for the Win32 API.
     *
     * The constructor opens and configures a COM port for 8N1 communication with
     * immediate-return reads and no hardware or software flow control.
     */
    class TinyWindowsAdapter {
    private:
        HANDLE _handle;

        TinyWindowsAdapter(
            const TinyWindowsAdapter&) = delete;

        TinyWindowsAdapter& operator=(
            const TinyWindowsAdapter&) = delete;

        /** @brief Close and invalidate the owned serial handle. */
        inline void close_port()
        {
            if (_handle != INVALID_HANDLE_VALUE) {
                CloseHandle(_handle);
                _handle = INVALID_HANDLE_VALUE;
            }
        }

    public:
        /**
         * @brief Open and configure a Windows serial port.
         *
         * The adapter remains closed when @p port is null, CreateFileA() fails, or
         * any required communication-state or timeout operation fails. Port names
         * above COM9 normally require Win32 device syntax such as "\\\\.\\COM10".
         *
         * @param port Null-terminated COM-port path.
         * @param baud Win32 baud-rate value such as CBR_9600 or CBR_115200.
         */
        explicit TinyWindowsAdapter(
            const char* port,
            DWORD baud = CBR_9600)
            : _handle(INVALID_HANDLE_VALUE)
        {
            if (!port) {
                return;
            }

            const HANDLE handle = CreateFileA(
                port,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (handle == INVALID_HANDLE_VALUE) {
                return;
            }

            _handle = handle;

            DCB state;
            ZeroMemory(&state, sizeof(state));
            state.DCBlength = sizeof(state);

            if (!GetCommState(_handle, &state)) {
                close_port();
                return;
            }

            state.BaudRate = baud;
            state.ByteSize = 8;
            state.StopBits = ONESTOPBIT;
            state.Parity = NOPARITY;
            state.fBinary = TRUE;
            state.fParity = FALSE;
            state.fOutxCtsFlow = FALSE;
            state.fOutxDsrFlow = FALSE;
            state.fDtrControl = DTR_CONTROL_DISABLE;
            state.fDsrSensitivity = FALSE;
            state.fTXContinueOnXoff = TRUE;
            state.fOutX = FALSE;
            state.fInX = FALSE;
            state.fErrorChar = FALSE;
            state.fNull = FALSE;
            state.fRtsControl = RTS_CONTROL_DISABLE;
            state.fAbortOnError = FALSE;

            if (!SetCommState(_handle, &state)) {
                close_port();
                return;
            }

            COMMTIMEOUTS timeouts;
            ZeroMemory(&timeouts, sizeof(timeouts));
            timeouts.ReadIntervalTimeout = MAXDWORD;
            timeouts.ReadTotalTimeoutMultiplier = 0;
            timeouts.ReadTotalTimeoutConstant = 0;
            timeouts.WriteTotalTimeoutMultiplier = 0;
            timeouts.WriteTotalTimeoutConstant = 0;

            if (!SetCommTimeouts(_handle, &timeouts)) {
                close_port();
                return;
            }

            if (!PurgeComm(
                _handle,
                PURGE_RXABORT |
                PURGE_RXCLEAR |
                PURGE_TXABORT |
                PURGE_TXCLEAR)) {
                close_port();
            }
        }

        /** @brief Close the owned Windows serial handle. */
        ~TinyWindowsAdapter()
        {
            close_port();
        }

        /** @brief Determine whether construction completed successfully. */
        inline bool isOpen() const
        {
            return _handle != INVALID_HANDLE_VALUE;
        }

        /**
         * @brief Return the number of bytes queued in the Windows receive buffer.
         *
         * Communication errors are cleared as required by ClearCommError(). Values
         * above INT_MAX are saturated because the TinyLink contract returns int.
         *
         * @return Nonnegative queued-byte count, or zero when closed or on error.
         */
        inline int available() const
        {
            if (!isOpen()) {
                return 0;
            }

            COMSTAT status;
            DWORD errors = 0;
            ZeroMemory(&status, sizeof(status));

            if (!ClearCommError(
                _handle,
                &errors,
                &status)) {
                return 0;
            }

            if (status.cbInQue >
                static_cast<DWORD>(INT_MAX)) {
                return INT_MAX;
            }

            return static_cast<int>(
                status.cbInQue);
        }

        /**
         * @brief Read one byte using immediate-return synchronous I/O.
         *
         * An empty receive buffer and ReadFile() errors are represented as -1 by
         * the current TinyLink adapter contract.
         *
         * @return Byte in the range 0 through 255, or -1 when no byte was read.
         */
        inline int read()
        {
            if (!isOpen()) {
                return -1;
            }

            uint8_t value = 0;
            DWORD bytesRead = 0;

            if (!ReadFile(
                _handle,
                &value,
                1,
                &bytesRead,
                nullptr) ||
                bytesRead != 1) {
                return -1;
            }

            return static_cast<int>(value);
        }

        /**
         * @brief Write one byte using synchronous Win32 I/O.
         *
         * The WriteFile() status and accepted byte count are reported.
         *
         * @param value Byte to write.
         */
        inline TinyWriteResult write(uint8_t value)
        {
            if (!isOpen()) {
                return tiny_write_result(1, 0, false);
            }

            DWORD bytesWritten = 0;

            const BOOL succeeded = WriteFile(
                _handle, &value, 1, &bytesWritten, nullptr);
            return tiny_write_result(
                1, static_cast<size_t>(bytesWritten), succeeded != FALSE);
        }

        /**
         * @brief Write a byte sequence using synchronous Win32 I/O.
         *
         * Win32 accepts a DWORD length, so larger requests are rejected rather
         * than truncated. A zero-length request does not call WriteFile(). A
         * null pointer is valid only when @p length is zero.
         *
         * @param data Bytes to write, or nullptr when @p length is zero.
         * @param length Number of bytes to write.
         */
        inline TinyWriteResult write(
            const uint8_t* data,
            size_t length)
        {
            if (length == 0) {
                return tiny_write_result(0, 0, true);
            }

            if (!isOpen() || !data ||
                length > static_cast<size_t>(MAXDWORD)) {
                return tiny_write_result(length, 0, false);
            }

            DWORD bytesWritten = 0;

            const BOOL succeeded = WriteFile(
                _handle, data, static_cast<DWORD>(length),
                &bytesWritten, nullptr);
            return tiny_write_result(
                length, static_cast<size_t>(bytesWritten), succeeded != FALSE);
        }

        /**
         * @brief Return Windows monotonic uptime in milliseconds.
         *
         * GetTickCount() wraps after approximately 49.7 days. TinyLink uses
         * unsigned elapsed-time subtraction, which supports this rollover.
         *
         * @return Current Windows uptime in milliseconds.
         */
        inline unsigned long millis() const
        {
            return static_cast<unsigned long>(
                GetTickCount());
        }
    };

} // namespace tinylink

#endif // _WIN32

#endif // TINYLINK_ADAPTERS_TINYWINDOWSADAPTER_H
