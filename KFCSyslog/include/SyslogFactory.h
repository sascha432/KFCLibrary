/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include "Syslog.h"

class SyslogFactory {
public:
	static constexpr uint32_t kDefaultValue = ~0;
	static constexpr uint16_t kDefaultPort = ~0;
	static constexpr uint16_t kZeroconfPort = 0;

	static Syslog *create(SemaphoreMutex &lock, const char *hostname, SyslogProtocol protocol, const String &host, uint16_t port = kDefaultPort);
	static Syslog *create(SemaphoreMutex &lock, const char *hostname, SyslogProtocol protocol, const String &hostOrPath, uint32_t portOrMaxFilesize = kDefaultValue);
};
