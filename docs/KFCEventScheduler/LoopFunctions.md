# LoopFunctions

`LoopFunctions` is the list of callbacks that the firmware's main loop executes **once per pass**. It
is a static class without instances (like `WiFiCallbacks`).

- Sources: `include/LoopFunctions.h`, `src/LoopFunctions.cpp` (paths are relative to the
  `KFCEventScheduler` folder)
- Included by `EventScheduler.h`, so `#include <EventScheduler.h>` is enough
- Related: [TaskQueue](TaskQueue.md) (thread and ISR safe deferral), [WiFiCallbacks](WiFiCallbacks.md)

## What the main loop does

The `loop()` of the firmware (`src/kfc_firmware.cpp` in the kfc_fw project) runs the entries of
`LoopFunctions::getVector()` on every pass:

1. for every entry that is not flagged for removal
   - `__Scheduler.run(Event::PriorityType::NORMAL)` is called,
   - then the callback of the entry
2. entries flagged by `remove()` are erased (under `InterruptLock`) and the vector is shrunk
3. `__Scheduler.run()` runs the remaining events
4. on ESP32 `run_scheduled_functions()` runs the functions added with `callOnce()`

The iteration is index based because a callback may add or remove entries while it runs.

| Call | Executed |
| --- | --- |
| `add()` / `LOOP_FUNCTION_ADD()` | every main loop pass until `remove()` |
| `callOnce()` | once, at the end of the current pass (`loop_end`) |
| `TaskQueue::process()` | wherever the consumer calls it, e.g. from a loop function |

## API

```cpp
class LoopFunctions {
public:
    class Entry;                                    // callback, callbackPtr, deleteCallback
    using Callback = std::function<void(void)>;
    using CallbackPtr = void(*)(void);
    using FunctionsVector = std::vector<Entry>;

    struct CallbackId {                             // 'this', a function pointer or any unique value
        CallbackId();
        CallbackId(const void *id);
        CallbackId(uint32_t id);
        CallbackId(CallbackPtr id);
    };

    static void clear();
    static void add(Callback callback, CallbackPtr callbackPtr);   // for lambda functions, use any unique pointer as id
    static void add(Callback callback, CallbackId id);
    static void add(CallbackPtr callbackPtr);                      // the function address is the id
    static void remove(CallbackPtr callbackPtr);
    static void remove(CallbackId id);
    static size_t size();
    static bool empty();
    static FunctionsVector &getVector();
    static bool callOnce(Callback callback);                       // == schedule_function(callback)
};
```

The macros add file and line in debug builds (see [Debugging](#debugging)):

| Macro | Expands to |
| --- | --- |
| `LOOP_FUNCTION_ADD(callback)` | `LoopFunctions::add(callback)` |
| `LOOP_FUNCTION_ADD_ARG(callback, arg)` | `LoopFunctions::add(callback, arg)` |

### Ids

- `add(CallbackPtr)` uses the address of the function as id.
- `add(Callback, CallbackId)` is for lambdas and member functions, the id is the second argument
  (`this` is the usual choice).
- The id identifies the entry, it is **not** part of the callback.

### add() is idempotent, but does not replace a callback

`add()` looks the id up (`std::find` over `Entry::callbackPtr`):

- id not found -> a new entry is appended,
- id found -> the existing entry is only re-enabled (`deleteCallback = false`), **its callback is
  left untouched**.

So calling `add()` twice with the same id keeps the *first* callback and the second one is silently
ignored. To replace a callback, `remove()` the id first (or use a different id). `remove()` only
flags the entry, so an immediate `add()` with the same id re-enables it with the old callback - the
erase happens at the end of the pass.

### remove()

`remove()` sets `deleteCallback = true`:

- the entry is not executed again, but it is only erased at the end of the current pass,
- a callback that is currently running always completes,
- `size()` and `empty()` ignore flagged entries,
- the vector is shrunk (`shrink_to_fit()`) after the erase, which is done under `InterruptLock`.

### callOnce()

`callOnce(callback)` is a one shot: the callback runs once at the end of the current loop pass
(`loop_end`) and is then gone. It is the recommended way to leave an event handler or an ISR
callback.

| | ESP8266 | ESP32 / `_MSC_VER` |
| --- | --- | --- |
| implementation | core `schedule_function()` (`cores/esp8266/Schedule.cpp`) | `LoopFunctions.cpp` (`scheduled_functions` vector) |
| called from an ISR | yes (pre-allocated nodes, interrupt lock) | no (plain `std::vector`, no lock) |
| limit | 32 pending entries | 32 pending entries |
| when the queue is full | returns `false` and drops the callback | returns `false` and drops the callback |
| cancel | not possible | not possible |

Use [TaskQueue](TaskQueue.md) when the callback has to survive a full queue, has to be cancellable
or has to be added from another task on the ESP32.

### Thread safety

`add()`, `remove()` and `clear()` manipulate a plain `std::vector` - they are **not** thread safe and
must run in the loop task. From the WiFi/sys task or an ISR, use `TaskQueue::push()` (thread and ISR
safe) or, on ESP8266, `callOnce()`.

## Examples

### Static function, runs every pass

```cpp
#include <EventScheduler.h>

static void pollButton() {
    // once per main loop
}

void setup() {
    LOOP_FUNCTION_ADD(pollButton);
}

void end() {
    LoopFunctions::remove(pollButton);
}
```

### Member function (`this` as id)

```cpp
class MyPlugin {
public:
    MyPlugin() {
        LOOP_FUNCTION_ADD_ARG([this]() {
            _loop();
        }, this);
    }

    ~MyPlugin() {
        LoopFunctions::remove(this);        // stops the callback, erased at the end of the pass
    }

private:
    void _loop() {
        // once per main loop
    }
};
```

### Draining a TaskQueue once per main loop

```cpp
TaskQueue _queue{8};

void setup() {
    LOOP_FUNCTION_ADD_ARG([this]() { _queue.process(4, 10); }, this);
}
```

See [TaskQueue](TaskQueue.md) for the details.

### One shot from an event handler

```cpp
// runs at the end of the current loop pass, never inside the WiFi event handler
LoopFunctions::callOnce([value]() {
    _apply(value);
});
```

## Debugging

`DEBUG_LOOP_FUNCTIONS` (0 by default, at the top of `LoopFunctions.h`) adds `_source`/`_line` to
every entry and makes the main loop measure each callback:

```
loop function time=25 source=my_plugin.cpp line=42
```

The threshold is `DEBUG_LOOP_FUNCTIONS_MAX_TIME` (20 ms). With the debug flag disabled
`LOOP_FUNCTION_ADD()`/`LOOP_FUNCTION_ADD_ARG()` pass only the callback and the id, so there is no
string/line overhead in release builds.

`getVector()` returns the raw vector (it is iterated by the main loop) - use it to inspect the
entries, not to modify the list.

## See also

- `include/TaskQueue.h` - thread and ISR safe queue for the loop task
- `include/Scheduler.h` - `Event::Timer` (timer callbacks in the main loop)
- kfc_fw firmware project: `src/kfc_firmware.cpp` (the main loop)
