# ManagedTimer

`Event::Timer` is the timer class that user code should use. Internally it is a thin wrapper around
`ManagedCallbackTimer`, which links it to the `CallbackTimer` owned by `Event::Scheduler` and removes
the scheduler timer when the `Event::Timer` is destroyed.

- Sources: `include/ManagedTimer.h` (`ManagedCallbackTimer`), `include/Timer.h` (`Event::Timer`),
  `include/Scheduler.hpp` (paths are relative to the `KFCEventScheduler` folder)
- Related: [CallbackTimer](CallbackTimer.md), [TaskQueue](TaskQueue.md)

## Why it exists

| Problem | Solution |
| --- | --- |
| a `CallbackTimer*` has to be rearmed/removed by the owner | `Event::Timer` keeps the pointer and exposes `add()`/`remove()` |
| a callback can fire after the owning object was deleted | `~Event::Timer()` removes the scheduler timer, `~ManagedCallbackTimer()` calls `remove()` |
| the scheduler has to know which timer belongs to which owner | `CallbackTimer::_timer` back-reference, set by `ManagedCallbackTimer` |

That makes a member `Event::Timer` the safe way to have a timer in a class:

```cpp
class Client {
public:
    Client() {
        _timer.add(Event::milliseconds(50), Event::RepeatType(true), [this](Event::CallbackTimerPtr) {
            _poll();
        });
    }

    ~Client() {
        // ~Event::Timer() -> ManagedCallbackTimer::remove() removes the timer from the scheduler
    }

private:
    void _poll() { }

    Event::Timer _timer;
};
```

This is the pattern that is used by the MQTT and syslog plugins instead of
`LoopFunctions::callOnce()` - the callback cannot run after the object is gone.

## Event::Timer

```cpp
namespace Event {

class Timer {
public:
    Timer(const Timer &) = delete;
    Timer(Timer &&) = delete;
    Timer &operator=(const Timer &) = delete;

    Timer();
    Timer(CallbackTimerPtr callbackTimer);
    ~Timer();                                   // remove()

    Timer &operator=(CallbackTimerPtr callbackTimer);

    void add(int64_t intervalMillis, RepeatType repeat, Callback callback, PriorityType priority = PriorityType::NORMAL);
    void add(milliseconds interval, RepeatType repeat, Callback callback, PriorityType priority = PriorityType::NORMAL);
    void add(const char *name, int64_t intervalMillis, RepeatType repeat, Callback callback, PriorityType priority = PriorityType::NORMAL);
    void add(const char *name, milliseconds interval, RepeatType repeat, Callback callback, PriorityType priority = PriorityType::NORMAL);

    // run the callback delayed and block further calls for delayMillis
    void throttle(uint32_t delayMillis, Callback callback, PriorityType priority = PriorityType::NORMAL);

    bool remove();

    operator bool() const;
    CallbackTimerPtr operator->() const noexcept;
    CallbackTimerPtr operator*() const noexcept;
};

}
```

| Method | Description |
| --- | --- |
| `add(...)` | creates the timer on the first call, afterwards it re-arms the existing one (`rearm()`). The **priority can not be changed** - use `remove()` and `add()` for that. |
| `add(..., Callback)` | replaces the callback when the timer is rearmed, `nullptr`/an empty callback keeps the current one. |
| `add(name, ...)` | `name` is only used by `DEBUG_OSTIMER` (it names the underlying `ETSTimerEx`). |
| `throttle()` | see below. |
| `remove()` | `true` when a timer was removed, `false` when the timer is not active. |
| `operator bool()` | `true` while a scheduler timer is attached. |
| `operator->()`, `operator*()` | the `CallbackTimerPtr` - **may be `nullptr`** if the timer is not active. |

The interval and repeat arguments are the same as for [CallbackTimer](CallbackTimer.md):
`Event::milliseconds(n)`, `RepeatType(true)` (unlimited), `RepeatType(false)`/`RepeatType(1)` (once),
`RepeatType(n)` (n times), `Event::seconds()`, `Event::minutes()`, `Event::hertz()`.

`Event::Timer` is non-copyable and non-movable (the managed timer stores a back-pointer to it).

### throttle()

```cpp
void throttle(uint32_t delayMillis, Callback callback, PriorityType priority = PriorityType::NORMAL);
```

- Timer active and currently blocked (`repeats left >= kNoRepeat`) -> the call is **ignored**.
- Timer active -> the callback is executed after `delayMillis` and further calls are blocked for
  that time (the wrapper disarms itself after calling the callback).
- Timer not active -> the callback runs once after 10 ms.

It is meant for events that must not be handled more often than a given interval (for example a
button or a value that changes in bursts).

## ManagedCallbackTimer

`ManagedCallbackTimer` is the internal link between an `Event::Timer` and a `CallbackTimer`. It is
move-only (`std::move`) and its destructor calls `remove()`, so the timer is removed when the owning
`Event::Timer` is destroyed.

```cpp
class ManagedCallbackTimer {
public:
    ManagedCallbackTimer(const ManagedCallbackTimer &) = delete;
    ManagedCallbackTimer &operator=(const ManagedCallbackTimer &) = delete;

    ManagedCallbackTimer();
    ManagedCallbackTimer(CallbackTimerPtr callbackTimer);
    ManagedCallbackTimer(CallbackTimerPtr callbackTimer, Timer *timer);   // sets callbackTimer->_timer

    ManagedCallbackTimer &operator=(ManagedCallbackTimer &&move) noexcept;

    ~ManagedCallbackTimer();                    // remove()

    operator bool() const;
    CallbackTimerPtr operator->() const noexcept;
    CallbackTimerPtr get() const noexcept;

    void clear();                               // unlink, the scheduler timer stays
    bool remove();                              // unlink and remove the scheduler timer
};
```

| Method | Description |
| --- | --- |
| ctor `(timer, owner)` | attaches to `callbackTimer`, sets `callbackTimer->_timer = timer` and releases a previously attached managed timer |
| `get()`, `operator->()` | the linked `CallbackTimerPtr` (`nullptr` when empty) |
| `operator bool()` | `true` when a `CallbackTimer` is linked |
| `clear()` | only clears the link (both directions), the scheduler timer keeps running |
| `remove()` | removes the timer from the scheduler and clears the link, returns `false` if there is nothing to remove |
| move assignment | releases the currently linked timer, then takes over the source (the source becomes empty) |

`remove()` handles the `disarm()` case: when the callback is currently running (`_insideCallback`),
the timer is disarmed and the scheduler removes it after the callback returns. From any other context
`Scheduler::_removeTimer()` is used, which disarms, completes (`done()`) and deletes the timer and
marks the slot for cleanup.

> `ManagedCallbackTimer` is an implementation detail of `Event::Timer`. Do not use it directly unless
> you implement something similar to `Event::Timer` yourself; a `CallbackTimer` created by
> `Scheduler::_add()` and handed over to a `ManagedCallbackTimer` has exactly one owner.

## Examples

### Interval timer with a fixed callback

```cpp
#include <EventScheduler.h>

Event::Timer _timer;

void setup() {
    _timer.add(Event::milliseconds(100), Event::RepeatType(true), [](Event::CallbackTimerPtr) {
        // every 100 ms in the main loop
    });
}

void end() {
    _timer.remove();
}
```

### Priorities

```cpp
// above NORMAL: no 250 ms limit, 15 ms soft runtime limit
_timer.add(Event::milliseconds(20), Event::RepeatType(true), [](Event::CallbackTimerPtr) {
    _readSensors();
}, Event::PriorityType::HIGH);
```

To change the priority, `remove()` the timer first - `add()` only re-arms an existing timer.

### Re-arm instead of adding a second timer

```cpp
void setInterval(uint32_t ms) {
    // timer exists -> rearm; not created yet -> create with default priority
    _timer.add(Event::milliseconds(ms), Event::RepeatType(true), [](Event::CallbackTimerPtr) {
        _tick();
    });
}
```

### Throttle

```cpp
void onValueChanged(int value) {
    _timer.throttle(250, [this, value](Event::CallbackTimerPtr) {
        _apply(value);
    });
}
```

## Pitfalls

- `add()` on an active timer **re-arms** it (the callback is replaced, the priority is kept). Use
  `remove()` + `add()` to change the priority.
- `operator->()`/`operator*()` return `nullptr` before the first `add()` and after `remove()`.
- A `Timer` must not outlive the `__Scheduler` (the scheduler is terminated at shutdown with
  `Scheduler::end()`; see `DISABLE_GLOBAL_EVENT_SCHEDULER`).
- `remove()` from inside the callback disarms the timer (through the managed timer) and the scheduler
  cleans it up after the callback - this is supported and safe.
- Destroying an `Event::Timer` from inside its own callback is supported as well
  (`ManagedCallbackTimer::remove()` detects `_insideCallback`).

## See also

- [CallbackTimer](CallbackTimer.md) - the timer object behind `Event::Timer`
- `include/Scheduler.h` - `Scheduler::add()`/`remove()`, `PriorityType`, runtime limits
- `include/Event.h` - `RepeatType`, `milliseconds`, `seconds()`, `minutes()`, `hertz()`
- [TaskQueue](TaskQueue.md) - handing work from a callback to the loop task
