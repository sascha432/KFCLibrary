/**
 * Author: sascha_lammers@gmx.de
 */


#pragma once

#include "SSIProxyStream.h"

#if DEBUG_SSI_PROXY_STREAM
#include "debug_helper_enable.h"
#else
#include "debug_helper_disable.h"
#endif

inline size_t SSIProxyStream::_available()
{
    if (_position >= _buffer.length()) {
        _readBuffer();
    }
    return _buffer.length() - _position;
}

inline int SSIProxyStream::read(uint8_t *buffer, size_t length)
{
    //__LDBG_printf("read_len=%d pos=%d length=%d size=%d", length, _position, _buffer.length(), _buffer.size());
    auto ptr = buffer;
    // A refill can consume everything it appended: when the data ends with a template token, the
    // buffer behind the token is removed and the output of the token is not available before the
    // next refill. Without a retry the copy loop below sees an empty buffer and returns 0 although
    // the stream is not at its end - the caller treats that as the end of the content and the
    // response is cut short. Retry while the file or a template can still deliver data
    for (uint8_t retry = 4; length && retry; retry--) {
        while (length && _available()) {
            auto copied = _copy(ptr, length);
            ptr += copied;
            length -= copied;
        }
        if (ptr != buffer || !(_file || _provider)) {
            break;
        }
    }
    return ptr - buffer;
}

inline int SSIProxyStream::available()
{
    if (!_file) {
        return false;
    }
    else if (_position < _buffer.length()) {
        return true;
    }
    return _file.available();
}

inline int SSIProxyStream::read()
{
    auto data = peek();
    if (data != -1) {
        _position++;
        _template.position--;
    }
    return data;
}

inline int SSIProxyStream::peek()
{
    int data;
    // see read(): a refill that consumed itself is not the end of the stream
    for (uint8_t retry = 4; retry; retry--) {
        if (_position >= _buffer.length()) {
            _readBuffer();
        }
        if (_position < _buffer.length()) {
            data = _buffer.get()[_position];
            return data;
        }
        if (!(_file || _provider)) {
            break;
        }
    }
    close();
    return -1;
}

#if DEBUG_SSI_PROXY_STREAM
#include "debug_helper_disable.h"
#endif
