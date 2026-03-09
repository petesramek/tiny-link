#ifndef TINY_WINDOWS_ADAPTER_H
#define TINY_WINDOWS_ADAPTER_H

#include <windows.h>
#include <stdint.h>
#include <iostream> // For error reporting

class TinyWindowsAdapter {
private:
    HANDLE _hSerial;
    bool _connected;

public:
    TinyWindowsAdapter(const char* portName, DWORD baud = CBR_9600) : _connected(false) {
        // 1. Attempt to open the serial port
        _hSerial = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (_hSerial == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                std::cerr << "TinyLink Error: Access denied. Is " << portName << " already open in another app?" << std::endl;
            } else if (error == ERROR_FILE_NOT_FOUND) {
                std::cerr << "TinyLink Error: Port " << portName << " not found." << std::endl;
            } else {
                std::cerr << "TinyLink Error: Failed to open port. Code: " << error << std::endl;
            }
            return;
        }

        // 2. Configure port settings (Baud, Parity, etc.)
        DCB dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        if (!GetCommState(_hSerial, &dcbSerialParams)) {
            std::cerr << "TinyLink Error: Could not get serial state." << std::endl;
            CloseHandle(_hSerial);
            return;
        }

        dcbSerialParams.BaudRate = baud;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity   = NOPARITY;

        if (!SetCommState(_hSerial, &dcbSerialParams)) {
            std::cerr << "TinyLink Error: Could not set serial parameters." << std::endl;
            CloseHandle(_hSerial);
            return;
        }

        // 3. Set non-blocking timeouts
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout         = MAXDWORD; // Return immediately if no data
        timeouts.ReadTotalTimeoutMultiplier  = 0;
        timeouts.ReadTotalTimeoutConstant    = 0;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant   = 0;

        if (!SetCommTimeouts(_hSerial, &timeouts)) {
            std::cerr << "TinyLink Error: Could not set timeouts." << std::endl;
            CloseHandle(_hSerial);
            return;
        }

        _connected = true;
    }

    ~TinyWindowsAdapter() {
        if (_hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(_hSerial);
        }
    }

    inline bool isOpen() { return _connected; }

    int available() {
        if (!_connected) return 0;
        COMSTAT comStat;
        DWORD errors;
        ClearCommError(_hSerial, &errors, &comStat);
        return (int)comStat.cbInQue;
    }

    int read() {
        if (!_connected) return -1;
        uint8_t b;
        DWORD bytesRead;
        if (ReadFile(_hSerial, &b, 1, &bytesRead, NULL) && bytesRead > 0) {
            return (int)b;
        }
        return -1;
    }

    void write(uint8_t c) {
        if (!_connected) return;
        DWORD bytesWritten;
        WriteFile(_hSerial, &c, 1, &bytesWritten, NULL);
    }

    void write(const uint8_t* b, size_t l) {
        if (!_connected) return;
        DWORD bytesWritten;
        WriteFile(_hSerial, b, (DWORD)l, &bytesWritten, NULL);
    }

    unsigned long millis() {
        return GetTickCount(); 
    }
};

#endif
