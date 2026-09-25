# OSTimer

`OSTimer` is the raw OS timer of the library: a base class that is subclassed and started with an
interval. The callback runs in the **OS timer context** (ESP8266: SDK timer / WiFi-sys context,
ESP32: the `esp_timer` task), not in the main loop. It is the layer below
[CallbackTimer](CallbackTimer.md)/[ManagedTimer](ManagedTimer.md) and it keeps running even while
the main loop is blocked.

- Sources: `include/OSTimer.h`, `include/OSTimer.hpp`, `include/OSTimer_esp8266.hpp`,
  `include/OSTimer_esp32.hpp`, `src/OSTimer.cpp` (paths are relative to the `KFCEventScheduler`
  folder)
- Included by `EventScheduler.h`, so `#include <EventScheduler.h>` is enough
- Related: [CallbackTimer](CallbackTimer.md) (scheduler owned timers), [TaskQueue](TaskQueue.md)

## Event::Timer or OSTimer?

| | `Event::Timer` / `CallbackTimer` | `OSTimer` |
| --- | --- | --- |
| runs in | main loop (`__Scheduler.run()`) | OS timer context |
| keeps running while the loop is blocked | no | yes |
| managed by the event scheduler | yes | no, the owner starts/stops it |
| min/max interval | `kMinDelay` .. `kMaxDelay` | same, `startTimer()` clamps |
| priority / runtime limits | yes | none, keep the callback short |
| can call `delay()`/blocking APIs | yes (loop task) | no |
| `detach()`/`stop` from anywhere | via the scheduler | `detach()` + `unlock(timer, timeout)` |

Use `OSTimer` for a timer that has to be independent of the main loop (5 ms polling, LED blinking,
RTC housekeeping); use `Event::Timer` for everything that touches the application state.

## OSTimer

```cpp
class OSTimer {
public:
    OSTimer();
    OSTimer(const char *name);                          // DEBUG_OSTIMER only
    virtual ~OSTimer();                                 // detach() + _etsTimer.done()

    virtual void run() = 0;                             // the callback

    void startTimer(Event::OSTimerDelayType delay, bool repeat, bool millis = true);
    virtual void detach();                              // stop the timer

    bool isRunning() const;
    operator bool() const;

    bool lock();
    static void unlock(OSTimer &timer);
    static void unlock(OSTimer &timer, uint32_t timeoutMicros);
    static bool isLocked(OSTimer &timer);

    static void ICACHE_FLASH_ATTR _OSTimerCallback(OSTimer *arg);
    SemaphoreMutex &getLock();

protected:
    ETSTimerEx _etsTimer;
    SemaphoreMutex _lock;
};
```

| Method | Description |
| --- | --- |
| `run()` | pure virtual, called by the OS timer. **Runs outside the main loop** - no blocking calls, no `delay()`, no flash/long running work. |
| `startTimer(delay, repeat, millis)` | creates and arms the timer. `delay` is clamped to `Event::kMinDelay` .. `Event::kMaxDelay`. `millis = true` (default) arms a millisecond timer, `millis = false` a microsecond timer (`esp_timer` on ESP32, the SDK `ms_flag` of `ets_timer_arm_new` on ESP8266). Calling it again re-arms the timer. |
| `detach()` | disarms the timer (the object stays usable, `startTimer()` can restart it). |
| `isRunning()` / `operator bool()` | the timer is armed. |
| `lock()` | marks the timer as locked: the callback is replaced by a no-op so a running `run()` is not interrupted. Returns `false` if the timer is already locked or (with `DEBUG_OSTIMER_FIND`) not registered. |
| `unlock(timer)` | releases the lock. |
| `unlock(timer, timeoutMicros)` | waits (with `optimistic_yield`) until a currently running `run()` has finished and then releases the lock - the safe way to stop a timer from another context. |
| `isLocked(timer)` | the lock state. |
| `getLock()` | the `SemaphoreMutex` used by `_OSTimerCallback()`. |

`Event::OSTimerDelayType` is `int64_t` on ESP32 and `uint32_t` elsewhere.

### Execution of the callback

`OSTimer::_OSTimerCallback()` (in `OSTimer.cpp`) is the function that the OS timer calls:

1. `MUTEX_LOCK_BLOCK(timer->getLock())` - the mutex is taken,
2. `timer->lock()` - if the timer is locked, the call is dropped,
3. the mutex is released and `run()` is executed (the timer stays "locked" so a concurrent
   `detach()`/`unlock(timer, timeout)` can wait for it),
4. the mutex is taken again and `OSTimer::unlock(*timer)` runs.

So `run()` never executes with the mutex held, and another context can use `isLocked()`/
`unlock(timer, timeoutMicros)` to wait for the end of `run()` before deleting the object.

### Real examples in the workspace

| Class | File | Interval |
| --- | --- | --- |
| `BlinkLEDTimer` | `include/blink_led_timer.h`, `src/blink_led_timer.cpp` | LED blink |
| `PinMonitor::PollingTimer` | `lib/KFCLibrary/KFCPinMonitor/src/polling_timer.h` | pin polling |
| `RTCMemoryManager::RtcTimer` | `lib/KFCLibrary/KFCResetDetector/include/RTCMemoryManager.h` | `startTimer(1000, true)` |
| `Mpr121Timer` | `src/plugins/weather_station/Mpr121Touchpad.h` | `startTimer(5, true)` |

## ETSTimerEx

`ETSTimerEx` is the low level wrapper around the platform timer. On ESP8266 it **is** an `ETSTimer`
(inherits from it, i.e. it can be used with the SDK directly), on ESP32 it wraps an
`esp_timer_handle_t`. It adds `create()`/`arm()`/`disarm()`/`done()` and consistency checks for the
timer state.

```cpp
struct ETSTimerEx {
    static constexpr uint32_t kUnusedMagic = 0x12345678;

    ETSTimerEx();
    ETSTimerEx(const char *name);                   // DEBUG_OSTIMER only
    ~ETSTimerEx();

    void create(ETSTimerExCallback callback, void *arg);
    void arm(int32_t delay, bool repeat, bool millis);

    bool isNew() const;      // not created yet
    bool isRunning() const;  // armed
    bool isDone() const;     // created and disarmed (ready to be used again)
    bool isLocked() const;
    void lock();
    void unlock();
    void disarm();
    void done();             // disarm + free the OS timer
    void clear();            // reset to a clean state

    static void end();       // terminate all OSTimer instances
};
```

| Platform | `create()` | `isNew()` / `isDone()` | `lock()` |
| --- | --- | --- | --- |
| ESP8266 | `ets_timer_disarm()` if needed + `ets_timer_setfn()` | derived from `timer_next`/`timer_arg`/`timer_period` and `kUnusedMagic` | replaces `timer_func` with `_EtsTimerLockedCallback` (no extra memory is used) |
| ESP32 | `esp_timer_create()` with `ESP_TIMER_TASK` dispatch | both mean `_timer == nullptr` | sets the `_locked` flag |

Platform notes:

- ESP8266: the timer is the SDK timer, `arm()` uses `ets_timer_arm_new(this, delay, repeat, millis)`,
  `disarm()`/`done()` use `ets_timer_disarm()`/`ets_timer_done()`. `timer_list` is the **SDK's own
  timer list**, `ETSTimerEx::find()` walks it.
- ESP32: `timer_list` is a `std::list<ETSTimerEx*>` maintained by the project, `arm()` starts a
  periodic or one-shot `esp_timer`, and every `esp_timer_*` return code is validated when
  `DEBUG_OSTIMER` is enabled.
- `ETSTimerEx::end()` stops all timers that were created by `OSTimer` (it walks `timer_list` and
  disarms the entries whose callback is `OSTimer::_OSTimerCallback` or `ETSTimerEx::_EtsTimerLockedCallback`) -
  used during shutdown.

> **ESP8266 trap:** `ets_timer_done()` asserts and then **spins forever** when the timer is still
> armed (`timer_next != 0xffffffff`). Always `disarm()` before `done()` - `ETSTimerEx::done()` does
> exactly that, so use `done()`/`clear()` rather than calling the SDK functions directly.

## Debugging

| Macro | Default | Effect |
| --- | --- | --- |
| `DEBUG_OSTIMER` | `0` (or `DEBUG_ALL`) | names for the timers, `_called`/`_calledWhileLocked` counters, `ETSTimerEx` state checks, `esp_timer` error validation |
| `DEBUG_OSTIMER_FIND` | `1` on ESP8266, `0` on ESP32 | `ETSTimerEx::find()` verifies that a timer still exists in `timer_list` before it is used (catches use-after-free) |
| `DEBUG_OSTIMER_VALIDATE(timer, func)` | no-op in release | wraps `esp_timer_*` calls |

`dumpTimers(Print &output)` prints all timers of `timer_list` (function/arg/period/expire and, for
timers owned by the event scheduler, the callback) and on ESP32 also the IDF timer list
(`esp_timer_dump`). It is exposed through the `+DUMPT` AT command (see `docs/AtModeHelp.md` in the
kfc_fw project).

## Example

```cpp
#include <EventScheduler.h>

class HeartbeatTimer : public OSTimer {
public:
    void start(uint32_t intervalMillis) {
        startTimer(intervalMillis, true);           // repeat
    }

    void run() override {
        // OS timer context: keep it short and do not block
        _ticks++;

        // hand the work over to the main loop
        // _queue.push([this]() { _publish(); });
    }

private:
    uint32_t _ticks = 0;
};

static HeartbeatTimer heartbeat;

void setup() {
    heartbeat.start(1000);
}

void loop() {
    // HeartbeatTimer::run() keeps running even if this loop is blocked
}
```

Stopping from another context:

```cpp
if (OSTimer::isLocked(heartbeat)) {
    OSTimer::unlock(heartbeat, 50000);   // wait up to 50 ms for run() to finish
}
heartbeat.detach();
```

## Pitfalls

- `run()` executes in the WiFi/sys (ESP8266) or `esp_timer` (ESP32) context: no `delay()`, no long
  running work, no flash writes, and only ISR safe APIs. Defer with [TaskQueue](TaskQueue.md) or
  `LoopFunctions::callOnce()`.
- Do not `delete` the `OSTimer` from inside `run()`; stop the timer with `detach()` or hand the
  delete over to the main loop.
- The minimum interval is 5 ms on ESP8266 and 1 ms on ESP32 (`Event::kMinDelay`), the maximum is
  0x68D7A3 ms (~1.9 h) on ESP8266 and 2^56-1 on ESP32.
- `ETSTimerEx` objects must be disarmed (`disarm()`/`done()`) before the memory is released.

## See also

- [CallbackTimer](CallbackTimer.md) - timer managed by the event scheduler
- [ManagedTimer](ManagedTimer.md) - `Event::Timer`, the recommended timer for application code
- `include/Event.h` - `Event::OSTimerDelayType`, `kMinDelay`, `kMaxDelay`
- `src/OSTimer.cpp` - `dumpTimers()`
