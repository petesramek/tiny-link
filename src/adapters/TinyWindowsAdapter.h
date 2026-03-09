/**
 * @file TinyWindowsAdapter.h
 * @brief Native Windows (Win32 API) serial adapter for TinyLink.
 */

#ifndef TINY_WINDOWS_ADAPTER_H
#define TINY_WINDOWS_ADAPTER_H

#ifdef _WIN32
#include <windows.h>
#include <stdint.h>

namespace tinylink {

    /**
     * @class TinyWindowsAdapter
     * @brief Direct Win32 implementation for Serial communication on Windows.
     */
    class TinyWindowsAdapter {
    private:
        HANDLE _h;    /**< Handle to the COM port */
        bool _ok;     /**< Connection status flag */

    public:
        /**
         * @brief Construct a new Windows Serial Adapter.
         * @param port The port name (e.g., "COM3" or "\\\\.\\COM10").
         * @param baud The baud rate (e.g., CBR_9600, CBR_115200).
         */
        TinyWindowsAdapter(const char* port, DWORD baud = CBR_9600) : _ok(false) {
            _h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (_h != INVALID_HANDLE_VALUE) {
                DCB dcb = {0}; 
                dcb.DCBlength = sizeof(dcb);
                if (GetCommState(_h, &dcb)) {
                    dcb.BaudRate = baud; 
                    dcb.ByteSize = 8;
                    dcb.StopBits = ONESTOPBIT;
                    dcb.Parity   = NOPARITY;
                    SetCommState(_h, &dcb);
                }
                
                // Set non-blocking read timeouts
                COMMTIMEOUTS ct = {MAXDWORD, 0, 0, 0, 0}; 
                SetCommTimeouts(_h, &ct);
                _ok = true;
            }
        }

        /** @brief Closes the serial handle upon destruction. */
        ~TinyWindowsAdapter() { 
            if (_h != INVALID_HANDLE_VALUE) CloseHandle(_h); 
        }

        /** @brief Returns true if the COM port was successfully opened. */
        inline bool isOpen()   { return _ok; }

        /** @brief Returns number of bytes waiting in the Windows RX buffer. */
        inline int available() { 
            COMSTAT s; DWORD e; 
            ClearCommError(_h, &e, &s); 
            return (int)s.cbInQue; 
        }

        /** @brief Reads a single byte. Returns -1 if no data is available. */
        inline int read() { 
            uint8_t b; DWORD r; 
            return (ReadFile(_h, &b, 1, &r, NULL) && r > 0) ? (int)b : -1; 
        }

        /** @brief Writes a single byte to the COM port. */
        inline void write(uint8_t c) { 
            DWORD w; WriteFile(_h, &c, 1, &w, NULL); 
        }

        /** @brief Writes a buffer of bytes to the COM port. */
        inline void write(const uint8_t* b, size_t l) { 
            DWORD w; WriteFile(_h, b, (DWORD)l, &w, NULL); 
        }

        /** @brief Returns system uptime in milliseconds via GetTickCount. */
        inline unsigned long millis() { return GetTickCount(); }
    };
}

#endif // _WIN32
#endif // TINY_WINDOWS_ADAPTER_H
