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
//      StrView(ptr).indexOf(F("probe"))                    // RAM       <=> PROGMEM
//
// supported sources: const char *, const __FlashStringHelper * (F/SPGM/FSPGM) and String
//
// NOTHING IS ALLOCATED, COPIED OR MODIFIED - the view stores one pointer (4 bytes, same as
// const char *) and every operation reads the operands only. No temporary String object, no
// buffer, no cached state, the compiler keeps the view in a register.
//
// the storage type is NOT tested at runtime, there is no is_PGM_P/strcmp_P dispatch either (a
// const char * can point to RAM or to PROGMEM, PSTR()/SPGM() produce PROGMEM pointers while a
// function parameter points to RAM). all operations use the functions of the platform (no custom
// or fork only helpers) and they access RAM and PROGMEM correctly:
//
//      ESP8266     strlen_P, pgm_read_byte (32 bit aligned access, no VLA/stack copy, no
//                  unaligned flash access) - the comparison is one byte wise pass over both
//      ESP32       strlen, strncmp, strncasecmp - PROGMEM is memory mapped there
//
// the case sensitivity is a compile time switch (template<bool _IgnoreCase>), every caller passes
// a literal - the requested variant is emitted instead of testing a bool inside the comparison
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
#include <strings.h>

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

    // same as equals(), the right operand can be a String, a char, a literal or F()/SPGM()/FSPGM()
    bool operator==(const StrView &str) const;
    bool operator!=(const StrView &str) const;
    bool operator==(char ch) const;
    bool operator!=(char ch) const;

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

    //
    // indexOf
    //

    // index of the first match, -1 if it is not found. an empty string is not found, a '\0' is
    // not found either (like equals/endsWith/startsWith)
    int indexOf(const StrView &str, size_t fromIndex = 0) const;
    int indexOfIgnoreCase(const StrView &str, size_t fromIndex = 0) const;
    int indexOf(char ch, size_t fromIndex = 0) const;
    int indexOfIgnoreCase(char ch, size_t fromIndex = 0) const;

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

    // compares length bytes, kNoLength (~0U) compares until the terminating NUL byte
    // the case sensitivity is a compile time switch, every caller passes a literal
    template<bool _IgnoreCase>
    static bool _equals(const char *str1, const char *str2, size_t length);
    static uint8_t _byteAt(const char *str, size_t index);

    static constexpr size_t kNoLength = ~0U;

    const char *_ptr;
};

// the view stores one pointer and nothing else, this is the reason why there is neither a cached
// length nor a storage type in here
static_assert(sizeof(StrView) == sizeof(const char *), "StrView must not use any memory");

// ----------------------------------------------------------------------------
// StrWrapper - the writable StrView of a String object
// ----------------------------------------------------------------------------
//
// StrView is read only and never modifies anything. StrWrapper is the writable variant: it is
// created from a String and modifies it in place, the comparisons of StrView are used for the
// result:
//
//      String name = _config.getAnimationName(type);       // one copy, the wrapper modifies it
//      S(name).replace(' ', '_');                          // = name.replace(' ', '_')
//      S(name).rtrim().toLowerCase();                      // = name.rtrim(), name.toLowerCase()
//      if (S(name).equalsIgnoreCase(slug)) { ... }         // no String temporary, no allocation
//
// replace(char, char) returns bool (like String::replace) and the other modifiers return the
// wrapper for chaining - only the bool cannot be chained with another modifier
//
// the class stores the String and the pointer to its buffer (like StrView) - every modification
// writes into the buffer of the String. the modifiers that shorten the string (trim/ltrim/rtrim)
// use String::remove() which keeps the length stored in the String in sync, the buffer itself is
// not reallocated by it (it moves the characters to the left and terminates the string)
//
// the Arduino core has no ltrim/rtrim at all and its replace()/trim() are void (only the fork of
// this project returns bool/String&, so calling them would not compile with a stock core), the
// wrappers are implemented in here using the buffer and remove() of the String
//
// only a String can be wrapped, its buffer is always writable RAM. the String must outlive the
// wrapper - the temporary of S(String(123)) lives until the end of the expression, a wrapper that
// is stored points to freed memory (like every other StrView temporary)

class StrWrapper : public StrView {
public:
    // the buffer of the String becomes the view, nothing else is stored
    StrWrapper(String &str);

    // an rvalue String can be wrapped for a single expression
    StrWrapper(String &&str);

    // same as String::replace(char find, char replace) - replaces all occurrences, returns true on
    // success (the String implementation cannot fail either)
    bool replace(char find, char replace);

    // same as String::trim/ltrim/rtrim - removes white space, the buffer shrinks in place
    StrWrapper &trim();
    StrWrapper &ltrim();
    StrWrapper &rtrim();

    // same for a single character instead of white space (String::trim(char)/ltrim(char)/rtrim(char)
    // do not exist in the Arduino core)
    StrWrapper &trim(char ch);
    StrWrapper &ltrim(char ch);
    StrWrapper &rtrim(char ch);

    // same as String::toLowerCase/toUpperCase - the length does not change
    StrWrapper &toLowerCase();
    StrWrapper &toUpperCase();

private:
    // the buffer of the view, a String's buffer is always RAM
    char *_buffer() const;

    // the String the view points to. the modifiers that shorten the string (trim/ltrim/rtrim) have
    // to update the length of the String as well, writing a NUL byte into the buffer is not enough
    // (String::length/charAt/setCharAt/+= use the length that is stored in the object)
    String &_string() const;

    String *_str;
};

// the wrapper stores the String and the buffer of the view
static_assert(sizeof(StrWrapper) == sizeof(const char *) + sizeof(String *), "StrWrapper must not use any memory");

// short alias, usable for declarations and temporaries
using S = StrWrapper;

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
    return _equals<false>(_ptr, str._ptr, kNoLength);
}

inline bool StrView::equalsIgnoreCase(const StrView &str) const
{
    if (!_ptr || !str._ptr) {
        return false;
    }
    return _equals<true>(_ptr, str._ptr, kNoLength);
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

inline bool StrView::operator==(const StrView &str) const
{
    return equals(str);
}

inline bool StrView::operator!=(const StrView &str) const
{
    return !equals(str);
}

inline bool StrView::operator==(char ch) const
{
    return equals(ch);
}

inline bool StrView::operator!=(char ch) const
{
    return !equals(ch);
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
    return _equals<false>(_ptr + (ownLength - length), str._ptr, kNoLength);
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
    return _equals<true>(_ptr + (ownLength - length), str._ptr, kNoLength);
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
    return _equals<false>(_ptr, str._ptr, length);
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
    return _equals<true>(_ptr, str._ptr, length);
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

inline int StrView::indexOf(const StrView &str, size_t fromIndex) const
{
    if (!_ptr || !str._ptr) {
        return -1;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return -1;      // an empty string is not found
    }
    const size_t ownLength = _length(_ptr);
    for (size_t i = fromIndex; (i + length) <= ownLength; i++) {
        if (_equals<false>(_ptr + i, str._ptr, length)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline int StrView::indexOfIgnoreCase(const StrView &str, size_t fromIndex) const
{
    if (!_ptr || !str._ptr) {
        return -1;
    }
    const size_t length = _length(str._ptr);
    if (length == 0) {
        return -1;
    }
    const size_t ownLength = _length(_ptr);
    for (size_t i = fromIndex; (i + length) <= ownLength; i++) {
        if (_equals<true>(_ptr + i, str._ptr, length)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline int StrView::indexOf(char ch, size_t fromIndex) const
{
    if (!_ptr || !ch) {
        return -1;
    }
    const size_t ownLength = _length(_ptr);
    for (size_t i = fromIndex; i < ownLength; i++) {
        if (_byteAt(_ptr, i) == static_cast<uint8_t>(ch)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline int StrView::indexOfIgnoreCase(char ch, size_t fromIndex) const
{
    if (!_ptr || !ch) {
        return -1;
    }
    const size_t ownLength = _length(_ptr);
    for (size_t i = fromIndex; i < ownLength; i++) {
        if (tolower(_byteAt(_ptr, i)) == tolower(static_cast<uint8_t>(ch))) {
            return static_cast<int>(i);
        }
    }
    return -1;
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

// compares length bytes, kNoLength (~0U) compares until the terminating NUL byte. the comparison
// stops at the NUL byte like strncmp/strcmp (both strings must be terminated)
template<bool _IgnoreCase>
inline bool StrView::_equals(const char *str1, const char *str2, size_t length)
{
#if ESP8266
    // one byte wise pass over both operands with the PROGMEM safe accessor. pgm_read_byte() reads
    // the aligned 32 bit word and returns one byte of it, this is correct for RAM and PROGMEM
    // (unaligned flash reads are emulated by the core) - the storage type does not have to be
    // known and no operand has to be copied
    while (length--) {
        const uint8_t ch1 = pgm_read_byte(str1++);
        const uint8_t ch2 = pgm_read_byte(str2++);
        if (ch1 != ch2) {
            if (!_IgnoreCase || tolower(ch1) != tolower(ch2)) {
                return false;
            }
        }
        if (ch1 == 0) {
            return true;        // both are terminated at the same position
        }
    }
    return true;
#else
    // PROGMEM is memory mapped on ESP32, the libc functions read both operands
    if (_IgnoreCase) {
        return strncasecmp(str1, str2, length) == 0;
    }
    return strncmp(str1, str2, length) == 0;
#endif
}

inline uint8_t StrView::_byteAt(const char *str, size_t index)
{
    return pgm_read_byte(str + index);
}

inline StrWrapper::StrWrapper(String &str) : StrView(str), _str(&str)
{
}

inline StrWrapper::StrWrapper(String &&str) : StrView(str), _str(&str)
{
}

inline String &StrWrapper::_string() const
{
    return *_str;
}

inline char *StrWrapper::_buffer() const
{
    // a String's c_str() is its own buffer (the SSO array or the heap), never PROGMEM or a literal
    // and always writable in practice - the core does the same cast in String::wbuffer(). the
    // pointer is read from the String, a cached pointer must not be used by the modifiers
    return const_cast<char *>(_str->c_str());
}

inline bool StrWrapper::replace(char find, char replace)
{
    auto ptr = _buffer();
    if (!ptr) {
        return false;
    }
    for (; *ptr; ptr++) {
        if (*ptr == find) {
            *ptr = replace;
        }
    }
    return true;
}

inline StrWrapper &StrWrapper::ltrim()
{
    auto &str = _string();
    auto ptr = _buffer();
    if (!ptr) {
        return *this;
    }
    size_t index = 0;
    while (index < str.length() && isspace(static_cast<uint8_t>(ptr[index]))) {
        index++;
    }
    if (index) {
        str.remove(0, static_cast<unsigned int>(index));
    }
    return *this;
}

inline StrWrapper &StrWrapper::rtrim()
{
    auto &str = _string();
    auto ptr = _buffer();
    if (!ptr) {
        return *this;
    }
    unsigned int length = str.length();
    unsigned int end = length;
    while (end && isspace(static_cast<uint8_t>(ptr[end - 1]))) {
        end--;
    }
    if (end != length) {
        str.remove(end, length - end);
    }
    return *this;
}

inline StrWrapper &StrWrapper::trim()
{
    return ltrim().rtrim();
}

inline StrWrapper &StrWrapper::ltrim(char ch)
{
    auto &str = _string();
    auto ptr = _buffer();
    if (!ptr || !ch) {
        return *this;
    }
    size_t index = 0;
    while (index < str.length() && ptr[index] == ch) {
        index++;
    }
    if (index) {
        str.remove(0, static_cast<unsigned int>(index));
    }
    return *this;
}

inline StrWrapper &StrWrapper::rtrim(char ch)
{
    auto &str = _string();
    auto ptr = _buffer();
    if (!ptr || !ch) {
        return *this;
    }
    unsigned int length = str.length();
    unsigned int end = length;
    while (end && ptr[end - 1] == ch) {
        end--;
    }
    if (end != length) {
        str.remove(end, length - end);
    }
    return *this;
}

inline StrWrapper &StrWrapper::trim(char ch)
{
    return ltrim(ch).rtrim(ch);
}

inline StrWrapper &StrWrapper::toLowerCase()
{
    auto ptr = _buffer();
    if (!ptr) {
        return *this;
    }
    for (; *ptr; ptr++) {
        *ptr = static_cast<char>(tolower(static_cast<uint8_t>(*ptr)));
    }
    return *this;
}

inline StrWrapper &StrWrapper::toUpperCase()
{
    auto ptr = _buffer();
    if (!ptr) {
        return *this;
    }
    for (; *ptr; ptr++) {
        *ptr = static_cast<char>(toupper(static_cast<uint8_t>(*ptr)));
    }
    return *this;
}
