/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <Buffer.h>
#include <EventScheduler.h>
#include "Syslog.h"

#if ESP8266
#    include <ESPAsyncTCP.h>
#elif ESP32
#    include <AsyncTCP.h>
#else
#    include <ESPAsyncTCP.h>
#endif

class SyslogTCP : public Syslog {
public:
    static constexpr uint16_t kDefaultPort = 514;
    static constexpr uint16_t kDefaultPortTLS = 6514;
    static constexpr uint16_t kMaxIdleSeconds = 30;
    static constexpr uint16_t kReconnectDelay = 3000;

public:
    SyslogTCP(SemaphoreMutex &lock, const char *hostname, const String &host, uint16_t port = kDefaultPort, bool useTLS = false);
    virtual ~SyslogTCP();

    virtual bool setupZeroConf(const String &hostname, const IPAddress &address, uint16_t port);
	virtual void transmit(const SyslogItem &item);
    virtual uint32_t getState(StateType state);
    virtual String getHostname() const;
    virtual uint16_t getPort() const;

public:
    inline static void __onPoll(void *arg, AsyncClient *client) {
        reinterpret_cast<SyslogTCP *>(arg)->_sendQueue();
    }

private:
    void _clear();
    void _onAck(size_t len, uint32_t time);
    void _onError(int8_t error);
    void _onTimeout(uint32_t time);
    void _onDisconnect();
    void _onPoll();
    void _sendQueue();

private:
    // calls _connect() after 3 seconds
    // subsequent calls do not increase the delay
    void _reconnect();
    // create new client and connect
    void _connect();
    // disconnect gracefully
    void _disconnect();
    // status report
    void _status(bool success, const __FlashStringHelper *reason = nullptr);

    // create (new) client
    void _allocClient();
    // destroy client
    // clear == true use the lock to clean the buffer
    void _freeClient(bool clear = true);

    bool hasQueue() const;

private:
    AsyncClient *_client;
    Event::Timer _reconnectTimer;
    char *_host;
    IPAddress _address;
    Buffer _buffer;                     // data to send
    uint16_t _port;
    uint16_t _ack;                      // number of bytes waiting for ack
    bool _useTLS;
};

inline void SyslogTCP::_reconnect()
{
    __DBG_printf("reconnect in %ums", kReconnectDelay);
    _clear();
    if (!_reconnectTimer) {
        // add new timer
        _Timer(_reconnectTimer).add(Event::milliseconds(kReconnectDelay), false, [this](Event::CallbackTimerPtr) {
            __DBG_printf("reconnecting");
            _connect();
        });
    }
}

inline SyslogTCP::~SyslogTCP()
{
    _Timer(_reconnectTimer).remove();
    _freeClient(false); // do not clean buffer, the object is already locked during destruction
    if (_host) {
        free(_host);
    }
}

inline String SyslogTCP::getHostname() const
{
    return _host ? _host : (IPAddress_isValid(_address) ? _address.toString() : emptyString);
}

inline uint16_t SyslogTCP::getPort() const
{
    return _port;
}

inline void SyslogTCP::_disconnect()
{
    _Timer(_reconnectTimer).remove();
    _client->close();
}

inline bool SyslogTCP::hasQueue() const
{
    MUTEX_LOCK_BLOCK(_lock) {
        return _buffer.length() && (_buffer.length() == _ack);
    }
    __builtin_unreachable();
}

