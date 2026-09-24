# TaskQueue

`TaskQueue` is a small, portable FIFO queue for tasks (`std::function<void(void)>`) in the
`KFCEventScheduler` library. It behaves like a FreeRTOS queue - producers can enqueue from any
context, a single consumer task drains the queue - and works the same way on **ESP32** and
**ESP8266**.

- Sources: `include/TaskQueue.h`, `src/TaskQueue.cpp` (paths are relative to the `KFCEventScheduler`
  folder)
- Included by `EventScheduler.h`, so `#include <EventScheduler.h>` is enough
- Header: `class TaskQueue` (global namespace, like `LoopFunctions` and `WiFiCallbacks`)

## When to use it

Use it whenever work has to leave the context it was created in, or when a callback must not run
inside a WiFi/sys/ISR callback. It closes the two gaps of the existing helpers:

| | `LoopFunctions::callOnce` | `Event::Timer` | `TaskQueue` |
| --- | --- | --- | --- |
| push from another task | ESP8266 yes (interrupt lock), ESP32 not thread safe (`std::vector`) | no (`Scheduler::_run()` iterates without a lock) | yes |
| push from an ISR | ESP8266 yes (max. 32 entries), ESP32 no | no | yes (`allocItem()` + `pushFromISR()`) |
| bounded / backpressure | silently drops after 32 entries | n/a | returns `FULL` (+ `dropped()` with `DEBUG_TASK_QUEUE`) |
| cancel / discard pending items | no | only the timer itself | `clear()` |

Typical use cases:

- a WebUI / MQTT / WiFi event must be applied by the main loop (single writer)
- a hardware interrupt has to hand data to a task
- a helper task (e.g. a parser) produces results for the loop

## Platform mapping

| | ESP32 | ESP8266 | `_MSC_VER` (mock) |
| --- | --- | --- | --- |
| bounded queue (`capacity > 0`) | `xQueueCreate()` / `xQueueSend()` / `xQueueReceive()` / `uxQueueMessagesWaiting()` / `vQueueDelete()` | intrusive list | intrusive list |
| `kUnlimited` | intrusive list + `portENTER_CRITICAL()` / `portEXIT_CRITICAL()` | intrusive list | intrusive list |
| task context lock | FreeRTOS queue lock | `ets_intr_lock()` / `ets_intr_unlock()` | `std::mutex` |
| ISR push | `xQueueSendFromISR()` + `portYIELD_FROM_ISR()` | append under `ets_intr_lock()` | not applicable |
| item type in the queue | `TaskQueue::Item *` (pointer, always trivially copyable) | | |

Both implementations are FIFO, the queue stores **pointers to items** - that is why an entry can be
queued from an ISR without any allocation.

## API

### Types

```cpp
class TaskQueue {
public:
    class Item;                             // opaque, created with allocItem()
    using ItemPtr = Item *;
    using Task = std::function<void(void)>;

    enum class ResultType : int8_t {
        SUCCESS = 0,                        // item has been queued
        FULL = -1,                          // capacity reached, item has not been queued
        EMPTY = -2,                         // queue is empty (pop() returns false)
        INVALID = -3,                       // no item or no task assigned
        NO_MEMORY = -4,                     // the item could not be allocated
    };

    static constexpr size_t kDefaultCapacity = 8;
    static constexpr size_t kUnlimited = 0;     // grow as needed
    static constexpr size_t kAllItems = ~0U;    // process() all queued items
};
```

### Producers

| Method | Context | Allocates | Ownership |
| --- | --- | --- | --- |
| `ResultType push(Task task)` | loop, any task | yes (item + closure) | queue takes it, a failed push deletes the item (the task is discarded) |
| `ResultType push(ItemPtr item)` | loop, any task | no | queue takes it **on success** |
| `ResultType pushFromISR(ItemPtr item)` | interrupt handler | no | queue takes it **on success** |
| `static ItemPtr allocItem(Task task)` | loop, any task | yes | caller, returns `nullptr` if the task is empty or on OOM |

`push()` is thread safe and returns `FULL` when the queue is full instead of dropping silently.
`pushFromISR()` contains no logging and no assertions, so it is safe to call with interrupts
disabled - but it must not allocate and it must only be called from an ISR (it uses
`portYIELD_FROM_ISR()` on ESP32).

A task is always a **lambda**; arguments are captured by the closure, there is no separate payload
type:

```cpp
_queue.push([this, value]{ _apply(value); });
```

### Consumer

The consumer must be a **single task**, normally the main loop. `pop()`/`process()`/`clear()` must
never be called from an ISR and `process()` is not reentrant.

| Method | Description |
| --- | --- |
| `bool pop(Task &task)` | removes one task, returns `false` if the queue is empty |
| `size_t process(size_t maxItems = kAllItems, uint32_t maxRuntimeMillis = 0)` | executes tasks inline, returns the number of executed tasks; stops after `maxItems` **or** `maxRuntimeMillis` (0 = no time limit) |
| `void clear()` | removes all queued items and releases the closures without executing them |

`process()` is the usual entry point. The runtime limit keeps the main loop, the watchdog and the
event scheduler responsive while long queues are drained:

```cpp
_queue.process(8, 20);      // at most 8 tasks or 20 ms per loop() pass
```

A task may push into the same queue (or another) while it runs - the lock is released before the
callback is executed.

### Processing once per main loop (loop function)

Instead of calling `process()` in `loop()` it can be registered as a **loop function**. The main
loop of the firmware iterates `LoopFunctions::getVector()` on every pass (`src/kfc_firmware.cpp` in
the kfc_fw project), so a registered callback is executed **exactly once per main loop** - in the
same task that `loop()` runs in, i.e. the same context as a manual `process()` call.

```cpp
#include <EventScheduler.h>

class MyPlugin {
public:
    void setup() {
        // 'this' is used as the unique id of the loop function
        LOOP_FUNCTION_ADD_ARG([this]() {
            _queue.process(4, 10);
        }, this);
    }

    void end() {
        LoopFunctions::remove(this);        // same id, the entry is removed at the end of the pass
    }

private:
    TaskQueue _queue{8};
};
```

For a static or free function the address of the function is the id, and the shorter macro can be
used:

```cpp
static TaskQueue queue{8};

static void processQueue() {
    queue.process();
}

void setup() {
    LOOP_FUNCTION_ADD(processQueue);        // once per loop() pass
}

// optional, from end()
LoopFunctions::remove(processQueue);
```

Notes:

- The queue is then drained by the loop task exactly once per pass, no `loop()` code is needed.
- `LoopFunctions::add()` is idempotent per id - adding the same id twice does not create a second
  entry, it only re-enables an entry that is pending removal.
- `LoopFunctions::remove()` does not erase immediately, it flags the entry (`deleteCallback`) and the
  main loop erases it after the pass, so a callback that is currently running completes.
- Use either the loop function or an explicit `process()` call in `loop()`, not both, otherwise the
  queue is drained twice per pass.
- `LOOP_FUNCTION_ADD()`/`LOOP_FUNCTION_ADD_ARG()` pass file and line in debug builds
  (`DEBUG_LOOP_FUNCTIONS` in `LoopFunctions.h`), which the main loop uses to report
  `loop function time=..` for callbacks that take longer than `DEBUG_LOOP_FUNCTIONS_MAX_TIME`.

### State

| Method | Description |
| --- | --- |
| `size_t size()` | number of queued items (exact) |
| `bool empty()` | `size() == 0` |
| `size_t capacity()` | configured capacity, `kUnlimited` if there is no limit |

Statistics - only compiled in with `DEBUG_TASK_QUEUE` (1 with `DEBUG_ALL`), see `TaskQueue.h`:

| Method | Description |
| --- | --- |
| `size_t dropped()` | items rejected with `FULL` |
| `size_t processed()` | tasks executed by `process()` |
| `size_t peakSize()` | highest `size()` that has been observed |
| `void resetStatistics()` | resets `dropped()`/`processed()`, sets `peakSize()` to the current `size()` |

The statistics counters are advisory: they can be off by a few counts while several producers push
concurrently. `clear()` does not count the discarded items as processed.

### Construction

```cpp
explicit TaskQueue(size_t capacity = kDefaultCapacity);     // 8 by default
```

`capacity` is clamped to a minimum of 1. `kUnlimited` (`0`) switches to an intrusive list without a
length limit - on ESP32 the FreeRTOS queue is not used in that case (a FreeRTOS queue has a fixed
length). The destructor clears the queue and deletes the FreeRTOS queue.

## Context rules

| Call | setup / loop | other task | ISR |
| --- | --- | --- | --- |
| `push(Task)`, `allocItem()` | yes | yes | no (allocates) |
| `push(ItemPtr)` | yes | yes | use `pushFromISR()` instead |
| `pushFromISR(ItemPtr)` | - | no (documented as ISR only) | yes |
| `pop()`, `process()`, `clear()` | yes (consumer) | no | no |
| `size()`, `capacity()`, `empty()` | yes | yes | no |
| `dropped()`, `processed()`, `peakSize()` | yes | yes (advisory, `DEBUG_TASK_QUEUE`) | no |

## Examples

### 1. Defer work from a callback into the main loop

```cpp
#include <EventScheduler.h>

class MyPlugin {
public:
    void setup() {
        // drain the queue once per main loop instead of calling process() in loop()
        LOOP_FUNCTION_ADD_ARG([this]() { _queue.process(4, 10); }, this);
    }

    // runs in the WiFi/sys or AsyncWebServer task
    void onWiFiEvent(WiFiCallbacks::EventType event, void *payload) {
        if (_queue.push([this, payload]{ _handleBanner(payload); }) != TaskQueue::ResultType::SUCCESS) {
            // queue full, the work has been discarded
            #if DEBUG_TASK_QUEUE
                __LDBG_printf("MyPlugin: queue full, dropped=%u", (unsigned)_queue.dropped());
            #else
                __LDBG_printf("MyPlugin: queue full");
            #endif
        }
    }

private:
    void _handleBanner(void *payload) {
        // safe: single writer, loop task
    }

    TaskQueue _queue{8};
};
```

### 2. Push from an interrupt handler

A `std::function` cannot be created inside an ISR, so the item is allocated ahead of time and only
queued by the handler:

```cpp
#include <EventScheduler.h>

namespace {
    TaskQueue pulses{16};
    TaskQueue::ItemPtr pulseItem = nullptr;     // pre-allocated, owned by the ISR until it is queued

    void sample() {
        // runs in the main loop task
    }

    void IRAM_ATTR pulseISR() {
        if (pulses.pushFromISR(pulseItem) == TaskQueue::ResultType::SUCCESS) {
            pulseItem = nullptr;                // ownership moved to the queue
        }
        // if the queue is full or the item is already queued, nothing happens (coalesced)
    }

    void processPulses() {
        pulses.process(8, 10);
        if (!pulseItem) {
            pulseItem = TaskQueue::allocItem([]{ sample(); });  // re-arm for the next interrupt
        }
    }
}

void setup() {
    pulseItem = TaskQueue::allocItem([]{ sample(); });
    attachInterrupt(digitalPinToInterrupt(kPin), pulseISR, RISING);
    LOOP_FUNCTION_ADD(processPulses);
}
```

Notes:

- The item is created with `allocItem()` **before** it is needed, so the ISR never allocates.
- A failed `pushFromISR()` leaves the item owned by the caller - do not free it inside the ISR.
- `pulseItem` is only re-armed by the loop task, so pulses that arrive while an item is queued are
  coalesced into one task. Use one pre-allocated item per slot (`pulseItem[2]`, ...) if every edge
  has to be processed.
- `pulseISR()` must be `IRAM_ATTR` and must not call anything that is not ISR safe.

### 3. Replace `LoopFunctions::callOnce`

```cpp
// before, silently drops after 32 pending callbacks and cannot be cancelled
LoopFunctions::callOnce([this]{ _refresh(); });

// now, bounded and observable
if (_queue.push([this]{ _refresh(); }) != TaskQueue::ResultType::SUCCESS) {
    // queue full
}
```

Because the queue is a class you can also keep several of them - one per subsystem - and drain them
with different limits, instead of sharing one global list.

### 4. Handle a full queue

```cpp
size_t runs = 0;
while (runs < 1000 && _queue.push([n = runs]{ _work(n); }) == TaskQueue::ResultType::SUCCESS) {
    runs++;
}
if (runs < 1000) {
    // the rest has not been queued, dropped() has been incremented
}
```

Alternatively use `kUnlimited` and accept that the queue grows with the workload:

```cpp
TaskQueue _queue{TaskQueue::kUnlimited};
```

## Footprint

The object file is only pulled out of the library when a translation unit actually references
`TaskQueue` - a firmware that never uses the class pays nothing. When it is used, expect roughly
1.5 KB of flash and 40 bytes of RAM (ESP32) / 0.9 KB and 32 bytes (ESP8266); the exact numbers
depend on which methods are referenced.

Each queued item is a heap allocation (the `Item` plus the closure of the `std::function`), so keep
the capacity small and use `pushFromISR()` with a pre-allocated item for interrupt driven producers.

## Notes and limitations

- The item type is `std::function<void(void)>`; there is no separate message/ID type - use captures
  or a lambda per message type.
- `process()` executes the callbacks inline. Long running callbacks block the consumer; use
  `maxRuntimeMillis` to spread the work over several loop passes.
- `push(Task)` needs the heap; it fails with `NO_MEMORY` when `new` returns `nullptr`.
- `push(ItemPtr)`/`pushFromISR(ItemPtr)` that fail with `FULL` or `INVALID` leave the item owned by
  the caller - free it from the loop task, not from an ISR.
- `EMPTY` is currently reserved; `pop()` reports an empty queue with `false`.
- The `_MSC_VER` (win32 mock) backend uses `std::mutex`; the ISR functions are not usable there.

## See also

- `include/EventScheduler.h` - umbrella header of the library
- `include/LoopFunctions.h` - loop functions (`LOOP_FUNCTION_ADD()`,
  `LOOP_FUNCTION_ADD_ARG()`, `LoopFunctions::remove()`) and `callOnce()`
- kfc_fw firmware project: `src/kfc_firmware.cpp` (the main loop that runs the loop functions and the
  event scheduler) and `docs/AtModeHelp.md` (`+DUMPT` prints the event scheduler timers)
