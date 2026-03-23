# Auto Update — Three Ways to Drive TinyLink

This example demonstrates **all three supported update modes** side by side so
you can choose the one that best fits your hardware and application constraints.

TinyLink's engine must be called periodically to process incoming serial bytes,
manage protocol timeouts, and fire callbacks.  Previously, interrupt-driven
updates required an explicit `enableAutoUpdate()` call.  Since v0.4.x that step
is **no longer required**: the constructor automatically registers the instance,
so every mode works with zero extra setup.

---

## 🔍 Mode Comparison

| | Mode 1 — Main Loop | Mode 2 — Timer ISR | Mode 3 — serialEvent() |
|---|---|---|---|
| **File** | `Mode1_MainLoop.ino` | `Mode2_TimerISR.ino` | `Mode3_SerialEvent.ino` |
| **How it works** | `link.update()` called manually each `loop()` | Static `autoUpdateISR()` hooked to a hardware timer | `autoUpdateISR()` called from Arduino's `serialEvent()` |
| **Hardware interrupt needed?** | ❌ No | ✅ Yes (timer) | ❌ No |
| **Third-party library needed?** | ❌ No | ✅ TimerOne (or platform equivalent) | ❌ No |
| **Setup calls needed** | None | Timer init + `attachInterrupt` | None |
| **`enableAutoUpdate()` needed?** | No | No | No |
| **Latency** | Depends on `loop()` speed | Fixed by timer period (e.g. 1 ms) | Fires as soon as bytes arrive |
| **Works while `loop()` is blocked?** | ❌ No | ✅ Yes | ❌ No (serialEvent blocks too) |
| **Best for** | Simple sketches, linear flow | Deterministic timing, busy main loop | No-timer boards, easy zero-setup event-driven RX |
| **Min RAM impact** | ~0 extra | ~0 extra | ~0 extra |

---

## 🚀 Quick Decision Guide

```
Does your board have a spare hardware timer?
  └─ Yes → Do you need deterministic timing regardless of loop() speed?
               └─ Yes → Mode 2 (Timer ISR)
               └─ No  → Mode 1 or Mode 3 (both equally good)
  └─ No  → Is your loop() free of long blocking calls (delay, etc.)?
               └─ Yes → Mode 3 (serialEvent — zero setup, reactive)
               └─ No  → Mode 2 if a timer is available; otherwise refactor
                         blocking code so update() can be called between steps
```

---

## Mode 1 — Main Loop (Universal, Zero Setup)

**File:** `Mode1_MainLoop.ino`

```cpp
void setup() {
    Serial.begin(9600);
    link.onDataReceived(onReceive);  // optional callback
}

void loop() {
    link.update();           // ← the only required call
    // ... your application logic ...
}
```

**Pros:**
- Works on **every** Arduino-compatible board
- No extra libraries or interrupt configuration
- Simplest code; easiest to debug

**Cons:**
- Engine is only called when `loop()` runs; avoid `delay()` or long blocking code

---

## Mode 2 — Timer ISR (Deterministic, Interrupt-Driven)

**File:** `Mode2_TimerISR.ino`

```cpp
#include <TimerOne.h>

void setup() {
    Serial.begin(9600);
    // Attach the static ISR — no enableAutoUpdate() needed.
    Timer1.initialize(1000);   // 1 ms period
    Timer1.attachInterrupt(TinyLink<SensorData, TinyArduinoAdapter>::autoUpdateISR);
}

void loop() {
    // update() is NOT called here — timer handles it automatically.
    // ... your application logic, including delay() if necessary ...
}
```

**Pros:**
- Protocol engine runs at a **fixed rate** independent of `loop()` timing
- Survives long blocking sections in application code
- ISR is ready immediately after construction; `enableAutoUpdate()` is optional

**Cons:**
- Requires a spare hardware timer and timer library
- Keep ISR callbacks short (avoid `Serial.print` inside callbacks on AVR)
- Only one TinyLink instance per `<T,Adapter>` type is ISR-driven at a time

---

## Mode 3 — serialEvent() (Zero Setup, No Interrupt Required)

**File:** `Mode3_SerialEvent.ino`

```cpp
// Arduino calls this automatically between loop() iterations when RX bytes
// are available — no setup needed beyond the function definition itself.
void serialEvent() {
    TinyLink<SensorData, TinyArduinoAdapter>::autoUpdateISR();
}

void setup() {
    Serial.begin(9600);
    link.onDataReceived(onReceive);
}

void loop() {
    // No update() needed — serialEvent handles it.
}
```

**Pros:**
- **Zero hardware interrupt required** — works on every Arduino board
- No extra library; `serialEvent()` is a built-in Arduino feature
- More reactive than a fixed timer: fires only when bytes actually arrive

**Cons:**
- `serialEvent()` is **not** called while `loop()` is inside a blocking function
  (e.g. `delay()`, `Wire.requestFrom()`, long `for` loops)
- Not available on all non-Arduino platforms (POSIX, Windows, ESP-IDF users
  should use Mode 1 or a platform-specific periodic callback instead)

---

## Multi-Instance Note

All three modes use the **same static ISR function** (`autoUpdateISR`).  When
you have multiple TinyLink instances of the same `<T,Adapter>` type, the
last-constructed instance is driven by default.  Call `enableAutoUpdate()` on
any instance to switch ISR dispatch to it:

```cpp
TinyLink<SensorData, TinyArduinoAdapter> linkA(adapterA);
TinyLink<SensorData, TinyArduinoAdapter> linkB(adapterB);
// linkB is the default ISR target (last constructed).

// To ISR-drive linkA instead:
linkA.enableAutoUpdate();
```

---

## Hardware Wiring (both devices)

```
Device A TX ──────────────── Device B RX
Device A RX ──────────────── Device B TX
Device A GND ─────────────── Device B GND
```

> **5 V ↔ 3.3 V boards**: add a voltage divider (1 kΩ + 2 kΩ) on the
> 5 V TX line to protect the 3.3 V RX pin.

---

## Which mode should I pick?

Start with **Mode 1**.  It works everywhere and requires the least code.
Switch to **Mode 3** if you want reactive, event-driven reception without
adding a timer library.  Use **Mode 2** only when your `loop()` timing is
unpredictable and a hardware timer is available.
