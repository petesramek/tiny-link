#ifndef TINY_WINDOWS_ADAPTER_H
#define TINY_WINDOWS_ADAPTER_H

#include <windows.h>

class TinyWindowsAdapter {
private:
    HANDLE _h;
    bool _ok;
public:
    TinyWindowsAdapter(const char* port, DWORD baud = CBR_9600) : _ok(false) {
        _h = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (_h != INVALID_HANDLE_VALUE) {
            DCB dcb = {0}; dcb.DCBlength = sizeof(dcb);
            GetCommState(_h, &dcb); dcb.BaudRate = baud; dcb.ByteSize = 8;
            SetCommState(_h, &dcb);
            COMMTIMEOUTS ct = {MAXDWORD, 0, 0, 0, 0}; SetCommTimeouts(_h, &ct);
            _ok = true;
        }
    }
    ~TinyWindowsAdapter() { if (_h != INVALID_HANDLE_VALUE) CloseHandle(_h); }
    inline bool isOpen()   { return _ok; }
    inline int available() { COMSTAT s; DWORD e; ClearCommError(_h, &e, &s); return s.cbInQue; }
    inline int read()      { uint8_t b; DWORD r; return (ReadFile(_h, &b, 1, &r, NULL) && r > 0) ? b : -1; }
    inline void write(uint8_t c) { DWORD w; WriteFile(_h, &c, 1, &w, NULL); }
    inline void write(const uint8_t* b, size_t l) { DWORD w; WriteFile(_h, b, (DWORD)l, &w, NULL); }
    inline unsigned long millis() { return GetTickCount(); }
};
#endif
