/**
 * Author: sascha_lammers@gmx.de
 */

#include <Arduino_compat.h>
#include "SyslogUDP.h"

#if DEBUG_SYSLOG
#    include <debug_helper_enable.h>
#else
#    include <debug_helper_disable.h>
#endif

SyslogUDP::SyslogUDP(SemaphoreMutex &lock, const char *hostname, const String &host, uint16_t port) :
    Syslog(lock, hostname),
    _host(nullptr),
    _port(port)
{
    if (host.length() && !_address.fromString(host)) {
        _host = strdup(host.c_str()); // resolve hostname later
    }
    __LDBG_printf("%s:%d address=%s", host.c_str(), port, _address.toString().c_str());
}

bool SyslogUDP::setupZeroConf(const String &hostname, const IPAddress &address, uint16_t port)
{
    if (_host) {
        free(_host);
        _host = nullptr;
    }
    if (IPAddress_isValid(address)) {
        _address = address;
    }
    else if (hostname.length()) {
        _host = strdup(hostname.c_str());
    }
    _port = port;
    return true;
}

void SyslogUDP::transmit(const SyslogItem &message)
{
    __LDBG_printf("msg=%s%s", _getHeader(millis()).c_str(), message.c_str());

    if (!WiFi.isConnected()) {
        return;
    }
    MUTEX_LOCK_BLOCK(_lock) {
        if (_host) {
            if (WiFi.hostByName(_host, _address) && IPAddress_isValid(_address)) { // try to resolve host and store its IP
                __LDBG_printf("host=%s resolved=%s", _host, _address.toString().c_str());
                free(_host);
                _host = nullptr;
            }
            else {
                // failed to resolve _host
                return;
            }
        }

        if (!_udp.beginPacket(_address, _port)) {
            _udp.stop();
            return;
        }
        {
            String header = _getHeader(millis());
            if (_udp.write((const uint8_t*)header.c_str(), header.length()) != header.length()) {
                _udp.stop();
                return;
            }
        }
        if ((_udp.write((const uint8_t*)message.c_str(), message.length()) == message.length()) && _udp.endPacket()) {
            return;
        }
        _udp.stop();
        return;
    }
}
