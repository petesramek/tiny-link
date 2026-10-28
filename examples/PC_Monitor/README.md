# PC serial monitor

`main.cpp` is a native desktop example that uses `TinyPosixAdapter` on POSIX systems and
`TinyWindowsAdapter` on Windows. Set `PORT` in the source to the serial device used by the
microcontroller. The Arduino adapter is not used by this desktop program.

The shared payload declaration is in `SharedData.h`; application message identifiers are
ordinary non-reserved bytes, not `MessageType` values. Build with the native C++11 toolchain for
the target operating system, adding `src/` to the include path. The POSIX adapter requires the
platform's POSIX serial APIs; the Windows adapter requires the Windows SDK.

The example calls `link.update()` in its loop and sends a command on a five-second interval.
The peer must use the same payload representation and acknowledge received application frames.
