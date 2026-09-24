# WiFiCallbacks

`WiFiCallbacks` is the WiFi event dispatcher of the firmware. Plugins register a callback for a
**bitmask** of events (connect / disconnect) and are notified after the WiFi stack reported them. It
is a static class without instances (like `LoopFunctions`).

- Sources: `include/WiFiCallbacks.h`, `src/WiFiCallbacks.cpp` (paths are relative to the
  `KFCEventScheduler` folder)
- Included by `EventScheduler.h`, so `#include <EventScheduler.h>` is enough
- Related: [LoopFunctions](LoopFunctions.md), [TaskQueue](TaskQueue.md)

## Events

`EventType` is a bitmask (`EventTypeEnum` provides the bitwise operators):

| Value | Event | Dispatched by the firmware |
| --- | --- | --- |
| `NONE` | `0x00`, no events | - |
| `CONNECTED` | `0x01`, a connection was established and an IP address was assigned | yes, it can occur again when a new IP is assigned (without a disconnect before) |
| `DISCONNECTED` | `0x02`, the connection was lost | yes, `CONNECTED` is always sent first |
| `MODE_CHANGE` | `0x04`, the WiFi mode changed | **not implemented**, no `callEvent()` exists for it |
| `CONNECTION` | `CONNECTED &#124; DISCONNECTED`, both | - |
| `ANY` | `CONNECTED &#124; DISCONNECTED &#124; MODE_CHANGE` | - |
| `CALLBACK_NOT_FOUND` | `-1`, return value of `remove()` | - |

`CONNECTION`/`ANY` are masks for `add()`/`remove()`, not events.

The events are dispatched by `kfc_fw_config.cpp` (`KFCFWConfiguration::_onWiFiGotIPCb`,
`_onWiFiDisconnectCb`). The dispatch is deferred out of the Arduino event handler with
`LoopFunctions::callOnce()`, so all callbacks run in the **main loop task**.

## API

```cpp
class WiFiCallbacks {
public:
    enum class EventType : int8_t { CALLBACK_NOT_FOUND = -1, NONE, CONNECTED, DISCONNECTED, MODE_CHANGE, CONNECTION, ANY };

    using Callback = std::function<void(EventType event, void *payload)>;
    using CallbackPtr = void(*)(EventType event, void *payload);

    static void clear();

    static EventType add(EventType events, Callback callback, CallbackPtr callbackPtr);
    static EventType add(EventType events, Callback callback, void *callbackPtr);   // id of the callback
    static EventType add(EventType events, CallbackPtr callbackPtr);                // the function is the id

    static EventType remove(EventType events, CallbackPtr callbackPtr);
    static EventType remove(EventType events, void *callbackPtr);

    static void callEvent(EventType event, void *payload);

    static CallbackVector &getVector();
};
```

| Method | Description |
| --- | --- |
| `add(events, callback, id)` | registers `callback` for `events`. An existing entry with the same id gets the new events **added** (`events |= existing`), the callback is not replaced. Returns the new event mask of the entry. |
| `add(events, callbackPtr)` | same, using a static/free function as callback and as id. |
| `remove(events, id)` | removes `events` from the entry: returns `NONE` if the entry was removed, the remaining mask if other events are still subscribed, or `CALLBACK_NOT_FOUND` if the id is unknown. |
| `callEvent(event, payload)` | invokes every entry whose mask intersects `event`. |
| `clear()` | removes all entries. |
| `getVector()` | raw access to the entries (public members `events`, `callback`, `callbackPtr`). |

### Ids

The id is the **callback pointer** (`CallbackPtr`), for lambdas and member functions you pass a
unique pointer as id - `this` is the usual choice. `add(Callback, CallbackPtr)` with a `nullptr`
pointer would match the first entry with a null id, so always pass a real id:

```cpp
WiFiCallbacks::add(WiFiCallbacks::EventType::CONNECTED, [this](WiFiCallbacks::EventType, void *) {
    _onConnected();
}, this);                       // <-- id, not optional
```

`add()` masks the requested events with `ANY` (`events &= ANY`), i.e. bits outside the WiFi event
mask are ignored.

### Removal while dispatching

`callEvent()` iterates the entries directly, so a callback may safely call `remove()` (including for
its own entry):

- while the dispatch loop is running, an entry whose mask became `NONE` is only marked,
- after the loop all `NONE` entries are erased and the vector is shrunk,
- `_locked` is only used for that bookkeeping - `add()`/`remove()` from **another task** are **not**
  synchronized, they must run in the loop task.

### Payload

`payload` is passed through unchanged, it is owned by the caller and is **only valid during the
callback**. The firmware passes the event object of the Arduino WiFi handler
(`(void *)&event` of a copy, see `KFCFWConfiguration::_onWiFiDisconnectCb`/`_onWiFiGotIPCb`), which
the firmware release `LoopFunctions::callOnce()` uses - it must not be stored.

## Examples

### Member function with `this` as id

```cpp
#include <EventScheduler.h>

class MyPlugin {
public:
    void setup() {
        WiFiCallbacks::add(WiFiCallbacks::EventType::CONNECTION, [this](WiFiCallbacks::EventType event, void *payload) {
            if (event == WiFiCallbacks::EventType::CONNECTED) {
                _onConnected();
            }
            else if (event == WiFiCallbacks::EventType::DISCONNECTED) {
                _onDisconnected();
            }
        }, this);
    }

    void end() {
        WiFiCallbacks::remove(WiFiCallbacks::EventType::ANY, this);
    }

private:
    void _onConnected() { }
    void _onDisconnected() { }
};
```

### Static function

```cpp
static void wifiCallback(WiFiCallbacks::EventType event, void *payload) {
    if (event == WiFiCallbacks::EventType::CONNECTED) {
        // ...
    }
}

void setup() {
    WiFiCallbacks::add(WiFiCallbacks::EventType::CONNECTION, wifiCallback);
}

void end() {
    WiFiCallbacks::remove(WiFiCallbacks::EventType::ANY, wifiCallback);
}
```

### Do not block the callback

The callbacks run from `LoopFunctions::callOnce()`, i.e. in the main loop, but they are still inside
the dispatch loop of `callEvent()`. Anything that blocks (DNS lookups, `WiFi.hostByName()`, flash
writes) or that runs on another task should be handed over to the loop function list or a
[TaskQueue](TaskQueue.md):

```cpp
WiFiCallbacks::add(WiFiCallbacks::EventType::CONNECTED, [this](WiFiCallbacks::EventType, void *) {
    _queue.push([this]() { _resolveAndConnect(); });        // pushed from the loop task, runs later
}, this);
```

## Notes

- `add()` **adds** events to an existing entry, it never replaces the callback. To change the
  callback, `remove()` the id first.
- An entry is matched by the pointer/id only - a lambda that is registered without an id cannot be
  removed again, which is why the id is mandatory in practice.
- `callEvent()` invokes the `std::function` if it is set, otherwise the function pointer.
- The dispatch is deferred into the main loop (`LoopFunctions::callOnce()`), but a callback is still
  called from inside the iteration of `callEvent()` - keep it short and do not block.

## See also

- `include/LoopFunctions.h` - the deferral used for the dispatch
- `include/TaskQueue.h` - handing work from a callback to the loop task
- kfc_fw firmware project: `src/kfc_fw_config.cpp` (`_onWiFiGotIPCb`, `_onWiFiDisconnectCb`)
