/**
 * @file TinyWindowsAdapter.h
 * @brief Native Windows (Win32 API) serial adapter for TinyLink.
 *
 * This adapter uses the Win32 API to provide a TinyLink-compatible
 * serial transport on Windows.
 */

#ifndef TINY_WINDOWS_ADAPTER_H
#define TINY_WINDOWS_ADAPTER_H

#ifdef _WIN32

#include <windows.h>
#include <stdint.h>
#include <stddef.h>

namespace tinylink {

    /**
     * @class TinyWindowsAdapter
     * @brief Direct Win32 implementation for serial communication on Windows.
     *
     * Adapter contract (required by TinyLink):
     *   - bool          isOpen();
     *   - int           available();
     *   - int           read();
     *   - void          write(uint8_t);
     *   - void          write(const uint8_t*, size_t);
     *   - unsigned long millis();
     */
    class TinyWindowsAdapter {
    private:
        HANDLE _h; /**< Handle to the COM port */
        bool   _ok; /**< Connection status flag */

    public:
        /**
         * @brief Construct a new TinyWindowsAdapter.
         * @param port The port name (e.g., "COM3" or "\\\\.\\COM10").
         * @param baud The baud rate (e.g., CBR_9600, CBR_115200).
         */
        explicit TinyWindowsAdapter(const char* port, DWORD baud = CBR_9600)
            : _h(INVALID_HANDLE_VALUE),
            _ok(false) {

            if (!port) return;

            _h = CreateFileA(
                port,
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

            if (_h != INVALID_HANDLE_VALUE) {
                DCB dcb;
                ::ZeroMemory(&dcb, sizeof(dcb));
                dcb.DCBlength = sizeof(dcb);

                if (GetCommState(_h, &dcb)) {
                    dcb.BaudRate = baud;
                    dcb.ByteSize = 8;
                    dcb.StopBits = ONESTOPBIT;
                    dcb.Parity = NOPARITY;
                    SetCommState(_h, &dcb);
                }

                // Non-blocking reads: return immediately if no data
                COMMTIMEOUTS ct;
                ::ZeroMemory(&ct, sizeof(ct));
                ct.ReadIntervalTimeout = MAXDWORD;
                ct.ReadTotalTimeoutMultiplier = 0;
                ct.ReadTotalTimeoutConstant = 0;
                ct.WriteTotalTimeoutMultiplier = 0;
                ct.WriteTotalTimeoutConstant = 0;
                SetCommTimeouts(_h, &ct);

                _ok = true;
            }
        }

        /**
         * @brief Closes the serial handle upon destruction.
         */
        ~TinyWindowsAdapter() {
            if (_h != INVALID_HANDLE_VALUE) {
                CloseHandle(_h);
                _h = INVALID_HANDLE_VALUE;
                _ok = false;
            }
        }

        /**
         * @brief Returns true if the COM port was successfully opened.
         */
        inline bool isOpen() {
            return _ok && _h != INVALID_HANDLE_VALUE;
        }

        /**
         * @brief Returns number of bytes waiting in the Windows RX buffer.
         */
        inline int available() {
            if (!isOpen()) return 0;
            COMSTAT s;
            DWORD  e;
            if (!ClearCommError(_h, &e, &s)) return 0;
            return static_cast<int>(s.cbInQue);
        }

        /**
         * @brief Reads a single byte. Returns -1 if no data is available.
         */
        inline int read() {
            if (!isOpen()) return -1;

            uint8_t b = 0;
            DWORD   r = 0;
            bool ok = ReadFile(_h, &b, 1, &r, NULL) ? true : false;

            return (ok && r > 0) ? static_cast<int>(b) : -1;
        }

        /**
         * @brief Writes a single byte to the COM port.
         */
        inline void write(uint8_t c) {
            if (!isOpen()) return;
            DWORD w = 0;
            (void)WriteFile(_h, &c, 1, &w, NULL);
        }

        /**
         * @brief Writes a buffer of bytes to the COM port.
         */
        inline void write(const uint8_t* b, size_t l) {
            if (!isOpen() || !b || l == 0) return;
            DWORD w = 0;
            (void)WriteFile(_h, b, static_cast<DWORD>(l), &w, NULL);
        }

        /**
         * @brief Returns system uptime in milliseconds via GetTickCount.
         */
        inline unsigned long millis() {
            return static_cast<unsigned long>(::GetTickCount());
        }
    };

} // namespace tinylink

#endif // _WIN32
#endif // TINY_WINDOWS_ADAPTER_H
