/**
  Author: sascha_lammers@gmx.de
*/

// StrView - string view for RAM, PROGMEM and String objects
//
//      StrView("abc/set").endsWith(F("/set"))              // literal   <=> PROGMEM
//      StrView(String(123)).equals(F("123"))               // String    <=> PROGMEM
//      StrView(F("a.html")).endsWith(".html")              // PROGMEM   <=> literal
//      StrView(topic).endsWith(SPGM(_effect_set))          // any       <=> PROGMEM
//      StrView(id).startsWith(F("ani-"))                   // any       <=> PROGMEM
//      StrView(ptr).endsWith('/')                          // any       <=> char
//
// supported sources: const char *, const __FlashStringHelper * (F/SPGM/FSPGM) and String
//
// NOTHING IS ALLOCATED, COPIED OR MODIFIED - the view stores one pointer (4 bytes, same as
// const char *) and every operation reads the operands only. No temporary String object, no
// buffer, no cached state, the compiler keeps the view in a register.
//
// the storage type is NOT tested at runtime, there is no is_PGM_P/strcmp_P dispatch and no
// compile time switch either (a const char * can point to RAM or to PROGMEM, PSTR()/SPGM()
// produce PROGMEM pointers while a function parameter points to RAM). all operations use the
// PROGMEM safe functions of the platform, they access RAM and PROGMEM correctly:
//
//      ESP8266     strlen_P, strncmp_P_P, strncasecmp_P_P, pgm_read_byte
//                  (32 bit aligned access, no VLA/stack copy, no unaligned flash access)
//      ESP32       strlen, strncmp, strncasecmp, pgm_read_byte - PROGMEM is memory mapped there
//
// this is always correct, no matter if the caller passes RAM or PROGMEM, and costs one pass over
// the compared range
//
// SEMANTICS - identical to the String implementation of this project
//      equals(nullptr) == false                   endsWith(nullptr) == false
//      equals("") on an empty string == true      endsWith("") == false
//      equals('\0') == false                      endsWith('\0') == false
//      an empty string does not end with an empty string, an empty string does not start with
//      an empty string either - startsWith("") == false
//      a default constructed view (StrView(), StrView((const char *)nullptr)) is not a string,
//      it has no length and every comparison returns false - like an invalid String
//
// NOTE: this is a view, the memory must stay valid and unchanged while it is used.
// StrView(String(123)) or StrView(localBuffer) may only be used inside the same expression,
// outside of it the view points to freed/invalid memory
//
// STRVIEW_ENABLE_VALIDATION (ESP8266 + DEBUG, on by default, define 0 to disable) is the only
// place where a pointer is checked - it reports pointers that are neither RAM/stack nor PROGMEM
// (String object, IPAddress, ROM, ...) before they are used. this catches the typical
// LoadStoreError and is compiled out completely in release builds

#pragma once

#include <Arduino_compat.h>
#include <stdint.h>
#include <pgmspace.h>
#include <string.h>
#ifndef _MSC_VER
#    include <strings.h>
#endif

#ifndef STRVIEW_ENABLE_VALIDATION
#    if ESP8266 && DEBUG
#        define STRVIEW_ENABLE_VALIDATION 1
#    else
#        define STRVIEW_ENABLE_VALIDATION 0
#    endif
#endif

class StrView {
public:
    StrView();

    // different sources, the storage type is not part of the view
    StrView(const char *str);
    StrView(const __FlashStringHelper *str);
    StrView(const String &str);

    // the memory of the view, may point to RAM or PROGMEM
    const char *c_str() const;

    // length in bytes without the terminating NUL byte
    size_t length() const;

    bool isEmpty() const;

    //
    // equals
    //

    bool equals(const StrView &str) const;
    bool equalsIgnoreCase(const StrView &str) const;
    bool equals(char ch) const;
    bool equalsIgnoreCase(char ch) const;

    //
    // endsWith
    //

    bool endsWith(const StrView &str) const;
    bool endsWithIgnoreCase(const StrView &str) const;
    bool endsWith(char ch) const;
    bool endsWithIgnoreCase(char ch) const;

    //
    // startsWith
    //

    bool startsWith(const StrView &str) const;
    bool startsWithIgnoreCase(const StrView &str) const;
    bool startsWith(char ch) const;
    bool startsWithIgnoreCase(char ch) const;

private:
#if STRVIEW_ENABLE_VALIDATION
    // defined in StrView.cpp - the debug macros cannot be used in here, this header is included
    // from inside the include chain of debug_helper.h
    static void _validate(const char *ptr);
#else
    static constexpr void _validate(const char *) {
    }
#endif

    static size_t _length(const char *str);

    // compares length bytes, kNoLength compares until the terminating NUL byte
    // length must not be 0 (strncmp_P_P returns an error code then)
    static bool _equals(const char *str1, const char *str2, size_t length, bool ignoreCase);
    static uint8_t _byteAt(const char *str, size_t index);

    static constexpr size_t kNoLength = ~0U;

    const char *_ptr;
};

// the view stores one pointer and nothing else, this is the reason why there is neither a cached
// length nor a storage type in here
static_assert(sizeof(StrView) == sizeof(const char *), "StrView must not use any memory");

// ----------------------------------------------------------------------------
// definitions outside of the class
// ----------------------------------------------------------------------------

inline StrView::StrView() : _ptr(nullptr)
{
}

inline StrView::StrView(const char *str) : _ptr(str)
{
    _validate(str);
}

inline StrView::StrView(const __FlashStringHelper *str) : _ptr(reinterpret_cast<const char *>(str))
{
    _validate(_ptr);
}

inline StrView::StrView(const String &str) : _ptr(str.c_str())
{
    _validate(_ptr);
}

inline const char *StrView::c_str() const
{
    return _ptr;
}

inline size_t StrView::length() const
{
    return _length(_ptr);
}

inline bool StrView::isEmpty() const
{
    return !_ptr || _byteAt(_ptr, 0) == 0;
}

inline bool StrView::equals(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    return _equals(_ptr, str._ptr, kNoLength, false);
}

inline bool StrView::equalsIgnoreCase(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    return _equals(_ptr, str._ptr, kNoLength, true);
}

inline bool StrView::equals(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    return _byteAt(_ptr, 0) == static_cast<uint8_t>(ch) && _byteAt(_ptr, 1) == 0;
}

inline bool StrView::equalsIgnoreCase(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    return tolower(_byteAt(_ptr, 0)) == tolower(static_cast<uint8_t>(ch)) && _byteAt(_ptr, 1) == 0;
}

inline bool StrView::endsWith(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return false;       // an empty string does not end with an empty string
    }
    const size_t ownLength = _length(_ptr);
    if (ownLength < length) {
        return false;
    }
    return _equals(_ptr + (ownLength - length), str._ptr, kNoLength, false);
}

inline bool StrView::endsWithIgnoreCase(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return false;
    }
    const size_t ownLength = _length(_ptr);
    if (ownLength < length) {
        return false;
    }
    return _equals(_ptr + (ownLength - length), str._ptr, kNoLength, true);
}

inline bool StrView::endsWith(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    const size_t length = _length(_ptr);
    return length != 0 && _byteAt(_ptr, length - 1) == static_cast<uint8_t>(ch);
}

inline bool StrView::endsWithIgnoreCase(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    const size_t length = _length(_ptr);
    return length != 0 && tolower(_byteAt(_ptr, length - 1)) == tolower(static_cast<uint8_t>(ch));
}

inline bool StrView::startsWith(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return false;       // an empty string does not start with an empty string
    }
    if (_length(_ptr) < length) {
        return false;
    }
    return _equals(_ptr, str._ptr, length, false);
}

inline bool StrView::startsWithIgnoreCase(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return false;
    }
    if (_length(_ptr) < length) {
        return false;
    }
    return _equals(_ptr, str._ptr, length, true);
}

inline bool StrView::startsWith(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    return _byteAt(_ptr, 0) == static_cast<uint8_t>(ch);
}

inline bool StrView::startsWithIgnoreCase(char ch) const
{
    if (!_ptr || !ch) {
        return false;
    }
    return tolower(_byteAt(_ptr, 0)) == tolower(static_cast<uint8_t>(ch));
}

inline size_t StrView::_length(const char *str)
{
    if (!str) {
        return 0;
    }
#if ESP8266
    // works for RAM and PROGMEM, unlike strlen it never reads an unaligned 32 bit word
    return strlen_P(str);
#else
    return strlen(str);
#endif
}

inline bool StrView::_equals(const char *str1, const char *str2, size_t length, bool ignoreCase)
{
#if ESP8266
    // byte wise comparison of RAM and/or PROGMEM with the PROGMEM safe accessors. the
    // strcmp_P_P macro cannot be used in here, it copies the first string to the stack (VLA)
    if (ignoreCase) {
        return strncasecmp_P_P(str1, str2, length) == 0;
    }
    return strncmp_P_P(str1, str2, length) == 0;
#else
    if (ignoreCase) {
        return strncasecmp(str1, str2, length) == 0;
    }
    return strncmp(str1, str2, length) == 0;
#endif
}

inline uint8_t StrView::_byteAt(const char *str, size_t index)
{
    return pgm_read_byte(str + index);
}
