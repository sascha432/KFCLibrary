/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#ifndef DEBUG_SYSLOG
#    define DEBUG_SYSLOG (0 || defined(DEBUG_ALL))
#endif

#include "SyslogParameter.h"
#include <Arduino_compat.h>
#include <Mutex.h>
#include <PrintString.h>
#include <functional>
#include <vector>

#if ESP32
#    define SYSLOG_PLUGIN_QUEUE_SIZE            4096
#elif ESP8266
#    define SYSLOG_PLUGIN_QUEUE_SIZE            1024
#endif

#ifndef SYSLOG_USE_RFC5424
#    define SYSLOG_USE_RFC5424 0 // 1 is not working ATM
#endif
#if SYSLOG_USE_RFC5424
#    define SEND_NILVALUE_IF_INVALID_TIMESTAMP 1
#    define SYSLOG_VERSION                     "1"
#    define SYSLOG_TIMESTAMP_FORMAT            "%FT%H:%M:%S."
#    define SYSLOG_TIMESTAMP_FRAC_FMT          "%03u"
#    define SYSLOG_SEND_BOM                    1 // UTF8 BOM
// TCP only
#else
// old/fall-back format
#    define SYSLOG_TIMESTAMP_FORMAT            "%h %d %T "
#    define SYSLOG_VERSION                     "" // not used
#    define SEND_NILVALUE_IF_INVALID_TIMESTAMP 0 // must be 0 for fallback
#endif

#define SYSLOG_FILE_TIMESTAMP_FORMAT "%FT%T%z"

#define SYSLOG_NIL_SP    "- "
#define SYSLOG_NIL_VALUE (SYSLOG_NIL_SP[0])

class Syslog;

#if defined(HAVE_KFC_FIRMWARE_VERSION) && HAVE_KFC_FIRMWARE_VERSION

#include "kfc_fw_config_types.h"

using SyslogProtocol = SyslogProtocolType;

#else

enum class SyslogProtocol {
    MIN = 0,
    NONE = MIN,
    UDP,
    TCP,
    TCP_TLS,
    FILE,
    MAX
};

#endif

#if 0
// facility/severity name/value pairs
typedef struct {
    PGM_P name;
    uint8_t value;
} SyslogFilterItemPair;

extern const SyslogFilterItemPair syslogFilterFacilityItems[] PROGMEM;
extern const SyslogFilterItemPair syslogFilterSeverityItems[] PROGMEM;

#endif

#if DEBUG_SYSLOG
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

class SyslogParameter;

class Syslog {
public:
    static constexpr size_t kMaxQueueSize = SYSLOG_PLUGIN_QUEUE_SIZE;

public:
    using SyslogItem = String;
    using SyslogQueue = std::vector<SyslogItem>;

    enum class StateType {
        NONE = 0,
        CAN_SEND,               // ready to send messages
        IS_SENDING,             // busy sending
        HAS_CONNECTION,         // connection based?
        CONNECTED,              // is connected?
    };

public:
    Syslog(const Syslog &) = delete;

    Syslog(SemaphoreMutex &lock, const char *hostname);
    virtual ~Syslog();

    virtual bool setupZeroConf(const String &hostname, const IPAddress &address, uint16_t port) = 0;
    virtual uint32_t getState(StateType type) = 0;

    inline bool canSend() {
        return getState(StateType::CAN_SEND);
    }
    inline bool isSending() {
        return getState(StateType::IS_SENDING);
    }
    inline bool isConnected() {
        return getState(StateType::CONNECTED);
    }
    inline bool hasConnection() {
        return getState(StateType::HAS_CONNECTION);
    }

    virtual void transmit(const SyslogItem &item) = 0;
    virtual String getHostname() const = 0;
    virtual uint16_t getPort() const = 0;

    // clear queue
    void clear();

    void setFacility(SyslogFacility facility);
    void setSeverity(SyslogSeverity severity);

    // add message
    void addMessage(String &&message);
    void addMessage(const String &message);

    // get dropped message
    uint32_t getDropped();

    // get queue item count
    uint32_t getQueueSize();

    // transmit one item and return items left
    uint32_t deliverQueue();

protected:
    String _getHeader(uint32_t millis) const;
    void _addTimestamp(PrintString &buffer, uint32_t millis, PGM_P format) const;

    void _addParameter(PrintString &buffer, const char *value) const;
    void _addParameter(PrintString &buffer, const String &value) const;
    void _addParameter(PrintString &buffer, const __FlashStringHelper *value) const;

private:
    template<typename _Ta>
    void __addParameter(PrintString &buffer, _Ta value) const
    {
        size_t len = 0;
        if (value) {
            len = buffer.print(value);
        }
        if (len) {
            buffer.write(' ');
        }
        else {
            buffer.print(F(SYSLOG_NIL_SP));
        }
    }

protected:
    SyslogParameter _parameter;
    String _message;
    SyslogQueue _queue;
    uint32_t _dropped;
    SemaphoreMutex &_lock;
};

inline Syslog::Syslog(SemaphoreMutex &lock, const char *hostname) :
    _parameter(hostname),
    _dropped(0),
    _lock(lock)
{
}

inline Syslog::~Syslog()
{
}

inline void Syslog::addMessage(String &&message)
{
    __LDBG_printf("msg=%s", message.c_str());
    MUTEX_LOCK_BLOCK(_lock) {
        size_t size = _queue.capacity() * sizeof(SyslogItem);
        for(const auto &item: _queue) {
            size += item.__getAllocSize();
            if (size > kMaxQueueSize) {
                __LDBG_printf("dropped=%u", size);
                _dropped++;
                return;
            }
        }
        __LDBG_printf("size=%u", size);
        _queue.emplace_back(std::move(message));
    }
    deliverQueue();
}

inline void addMessage(const String &message)
{
    addMessage(std::move(String(message)));
}

inline uint32_t Syslog::getDropped()
{
    MUTEX_LOCK_BLOCK(_lock) {
        return _dropped;
    }
    __builtin_unreachable();
}

inline uint32_t Syslog::getQueueSize()
{
    MUTEX_LOCK_BLOCK(_lock) {
        return _queue.size();
    }
    __builtin_unreachable();
}

inline uint32_t Syslog::deliverQueue()
{
    if (!canSend() || isSending()) {
        // delay delivery
        return getQueueSize();
    }

    SyslogItem item;
    MUTEX_LOCK_BLOCK(_lock) {
        if (_queue.empty()) {
            return 0;
        }
        item = _queue.front();
        _queue.erase(_queue.begin());
    }

    // transmit message
    transmit(item);

    return getQueueSize();
}

inline void Syslog::clear()
{
    __LDBG_printf("clear=%u", _queue.size());
    MUTEX_LOCK_BLOCK(_lock) {
        _queue.clear();
    }
}

inline void Syslog::setFacility(SyslogFacility facility)
{
    _parameter.setFacility(facility);
}

inline void Syslog::setSeverity(SyslogSeverity severity)
{
    _parameter.setSeverity(severity);
}


inline void Syslog::_addParameter(PrintString &buffer, const char *value) const
{
    __addParameter(buffer, value);
}

inline void Syslog::_addParameter(PrintString &buffer, const String &value) const
{
    __addParameter(buffer, value.c_str());
}

inline void Syslog::_addParameter(PrintString &buffer, const __FlashStringHelper *value) const
{
    __addParameter(buffer, value);
}

#if DEBUG_SYSLOG
#    include <debug_helper_disable.h>
#endif
