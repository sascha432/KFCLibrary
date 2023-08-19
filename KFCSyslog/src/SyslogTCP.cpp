/**
 * Author: sascha_lammers@gmx.de
 */

#include <Arduino_compat.h>
#include <EventScheduler.h>
#include <misc.h>
#include "SyslogParameter.h"
#include "Syslog.h"
#include "SyslogTCP.h"

#if DEBUG_SYSLOG
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

SyslogTCP::SyslogTCP(SemaphoreMutex &lock, const char *hostname, const String &host, uint16_t port, bool useTLS) :
    Syslog(lock, hostname),
    _client(nullptr),
    _host(nullptr),
    _port(port),
    _ack(0),
    _useTLS(useTLS)
{
    __LDBG_printf("this=%p", this);
    __LDBG_printf("%p:%d TLS %d", host, _port, _useTLS);
    if (host.length() && !_address.fromString(host)) {
        _host = strdup(host.c_str());
    }
}

void SyslogTCP::transmit(const SyslogItem &message)
{
    #if DEBUG_SYSLOG
        if (_ack) {
            __LDBG_printf_E("ack=%u buffer=%u", _ack, _buffer.length());
            _ack = -1;
            _clear();
        }
    #endif

    _connect();

    MUTEX_LOCK_BLOCK(_lock) {
        __LDBG_printf("msg=%s%s", _getHeader(millis()).c_str(), message.c_str());
        __LDBG_assert(_ack == 0);

        _buffer = _getHeader(millis());
        auto ptr = message.c_str();
        char ch;
        while((ch = *ptr++) != 0) {
            switch(ch) {
            case '\n':
                _buffer.write(F("\\n"));
                break;
            case '\r':
                _buffer.write(F("\\r"));
                break;
            case 0:
                _buffer.write(F("\\x00"));
                break;
            default:
                _buffer.write(ch);
                break;
            }
        }
        _buffer.write('\n');
        _ack = _buffer.length();
    }
}

uint32_t SyslogTCP::getState(StateType state)
{
    switch (state) {
    case StateType::CAN_SEND:
        return (_port != 0) && (_host || IPAddress_isValid(_address)) && WiFi.isConnected();
    case StateType::IS_SENDING: {
        MUTEX_LOCK_BLOCK(_lock) {
            return _ack != 0;
        }
    }
    case StateType::CONNECTED:
        return _client && _client->connected();
    case StateType::HAS_CONNECTION:
        return true;
    default:
        break;
    }
    return false;
}

void SyslogTCP::_clear()
{
    MUTEX_LOCK_BLOCK(_lock) {
        if (_ack != _buffer.length()) { // discard buffer that has been send partially
            _buffer.clear();
            _ack = 0;
        }
    }
}

bool SyslogTCP::setupZeroConf(const String &hostname, const IPAddress &address, uint16_t port)
{
    __LDBG_printf("hostname=%s address=%s port=%u", hostname.c_str(), address.toString().c_str(), port);
    if (_host) {
        free(_host);
        _host = nullptr;
    }
    _address = IPAddress();
    if (IPAddress_isValid(address)) {
        _address = address;
    }
    else if (hostname.length()) {
        _host = strdup(hostname.c_str());
    }
    _port = port;

    if (hasQueue()) {
        // check if we have a queue and connect
        _connect();
    }
    else {
        // terminate any existing client
        _freeClient();
    }
    return true;
}


void SyslogTCP::_onTimeout(uint32_t time)
{
    if (hasQueue()) {
        _status(false, F("timeout, reconnecting"));
        _reconnect();
    }
    else {
        _status(false, F("timeout"));
        _disconnect();
    }
}

// connect to remote host if connection is not established
void SyslogTCP::_connect()
{
    if (_port == 0) {
        __LDBG_printf("port==0");
        _freeClient();
        return;
    }
    #if DEBUG_SYSLOG
        if (_client) {
            __LDBG_printf("connected=%u port=%u connecting=%u can_send=%u state=%s", _client->connected(), _port, _client->connecting(), _client->canSend(), _client->stateToString());
        }
    #endif
    _allocClient();

    #if ASYNC_TCP_SSL_ENABLED
    #   define USE_TLS , _useTLS
    #else
    #   define USE_TLS
    #endif

    bool result;
    if (_host) {
        result = _client->connect(_host, _port USE_TLS);
    }
    else {
        result = _client->connect(_address, _port USE_TLS);
    }
    if (!result) {
        _status(false, F("connect failed"));
    }
}


void SyslogTCP::_allocClient()
{
    _freeClient();

    _client = new AsyncClient();
    _client->onConnect(__onPoll, this);
    _client->onPoll(__onPoll, this);
    _client->onDisconnect([](void *arg, AsyncClient *client)  {
        reinterpret_cast<SyslogTCP *>(arg)->_onDisconnect();
    }, this);
    _client->onAck([](void *arg, AsyncClient *client, size_t len, uint32_t time) {
        reinterpret_cast<SyslogTCP *>(arg)->_onAck(len, time);
    }, this);
    _client->onError([](void *arg, AsyncClient *client, int8_t error) {
        reinterpret_cast<SyslogTCP *>(arg)->_onError(error);
    }, this);
    _client->onTimeout([](void *arg, AsyncClient *client, uint32_t time) {
        reinterpret_cast<SyslogTCP *>(arg)->_onTimeout(time);
    }, this);
    _client->setRxTimeout(kMaxIdleSeconds);
}

void SyslogTCP::_freeClient(bool clear)
{
    if (_client) {
        _Timer(_reconnectTimer).remove();
        // remove all callbacks before aborting the connection
        _client->onConnect(nullptr);
        _client->onPoll(nullptr);
        _client->onDisconnect(nullptr);
        _client->onAck(nullptr);
        _client->onError(nullptr);
        _client->onTimeout(nullptr);
        // close and abort forcefully
        _client->close(true);
        _client->abort();
        delete _client;
        _client = nullptr;
    }
    if (clear) {
        _clear();
    }
}

void SyslogTCP::_status(bool success, const __FlashStringHelper *reason)
{
    #if DEBUG_SYSLOG
        if (success) {
            __DBG_printf("success");
        }
        else {
            __DBG_printf("failure");
        }
    #endif
    MUTEX_LOCK_BLOCK(_lock) {
        _buffer.clear();
        _ack = 0;
    }
}

void SyslogTCP::_sendQueue()
{
    bool reconnect = false;
    MUTEX_LOCK_BLOCK(_lock) {
        if (_buffer.length() && _client->connected() && _client->canSend()) {
            auto toSend = std::min(_client->space(), _buffer.length());
            auto written = _client->write(_buffer.cstr_begin(), toSend);
            if (written == toSend) {
                // remove what has been written to the socket
                if (written == _buffer.length()) {
                    _buffer.clear();
                }
                else {
                    _buffer.removeAndShrink(0, written);
                }
            }
            else {
                reconnect = true;
            }
        }
    }
    if (reconnect) {
        _status(false, F("socket write error, reconnecting"));
        _reconnect();
    }
}

void SyslogTCP::_onAck(size_t len, uint32_t time)
{
    __LDBG_printf("len=%u time=%u ack=%u buffer=%u state=%s", len, time, _ack, _buffer.length(), _client->stateToString());
    if (hasQueue()) {
        if (len > _ack) {
            _status(false, F("invalid ACK size, reconnecting"));
            _reconnect();
        }
        else {
            size_t bufferLen;
            MUTEX_LOCK_BLOCK(_lock) {
                _ack -= len;
                bufferLen = _buffer.length();
            }
            if (_ack == 0 && bufferLen == 0) {
                // report success and remove queueId
                _status(true);
            }
             else if (bufferLen) {
                 // try to send more data
                 _sendQueue();
             }
        }
    }
    else {
        // we do not expect any ACK packet
        // terminate client
        _freeClient();
    }
}

void SyslogTCP::_onError(int8_t error)
{
    auto errorStr = _client->errorToString(error);
    _status(false, reinterpret_cast<const __FlashStringHelper *>(errorStr));
    _freeClient();
}

void SyslogTCP::_onDisconnect()
{
    if (hasQueue()) {
        _status(false, F("reconnecting"));
        _reconnect();
    }
    else {
        _freeClient();
    }
}
