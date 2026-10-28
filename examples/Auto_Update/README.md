# TinyLink update examples

The current TinyLink API has no `autoUpdateISR()` or `enableAutoUpdate()` hooks. Call
`link.update()` on each `TinyLink` instance regularly from the application.

- `Mode1_MainLoop.ino` shows the supported main-loop update pattern.
- `Mode2_TimerISR.ino` has been converted to call `link.update()` from `loop()`. It is not a
  timer-ISR example; TinyLink processing must not run inside a hardware interrupt.
- `Mode3_SerialEvent.ino` calls the instance's `link.update()` from Arduino's `serialEvent()`.
  This is cooperative scheduling between loop iterations and does not run while `loop()` is
  blocked.

Callbacks registered with `onDataReceived()` run from the context that calls `update()`.
Keep callback work safe for that context, and do not assume callbacks are deferred out of an
interrupt unless the application itself defers the update call.
