/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <stdlib.h>
#include "type_traits.h"

namespace stdex {

    // random number from <from> to <to> - 1, <to> must be greater than <from>
    inline int randint(int from, int to)
    {
        return from + (rand() % (to - from));
    }

    //
    // bit operations and iteration for an enum class
    //
    // the operators return a new value, assign it to change a variable:
    //
    //  events = EnumType(events) | Enum::A;       // add bits
    //  events = EnumType(events) ^ Enum::A;       // remove bits
    //
    // EnumType(events) |= ... would only change a temporary copy, the compound operators do not exist
    //
    // for(auto value: enum_type<Enum>()) {}      // iterates from Enum::MIN to Enum::MAX - 1
    //
    template<typename _Enum>
    struct enum_type {
        using Type = enum_type<_Enum>;
        using Enum = _Enum;

        constexpr enum_type() : _enum(static_cast<_Enum>(0))
        {
        }

        constexpr enum_type(_Enum e) : _enum(e)
        {
        }

        constexpr operator _Enum() const
        {
            return _enum;
        }

        constexpr operator bool() const
        {
            return to_underlying(_enum) != 0;
        }

        constexpr Type operator|(Type e) const
        {
            return static_cast<_Enum>(to_underlying(_enum) | to_underlying(e._enum));
        }

        constexpr Type operator&(Type e) const
        {
            return static_cast<_Enum>(to_underlying(_enum) & to_underlying(e._enum));
        }

        // removes the bits of e, *this & ~e
        constexpr Type operator^(Type e) const
        {
            return static_cast<_Enum>(to_underlying(_enum) & ~to_underlying(e._enum));
        }

        constexpr Type operator~() const
        {
            return static_cast<_Enum>(~to_underlying(_enum));
        }

        constexpr Type &operator++()
        {
            _enum = static_cast<_Enum>(to_underlying(_enum) + 1);
            return *this;
        }

        constexpr Type &operator--()
        {
            _enum = static_cast<_Enum>(to_underlying(_enum) - 1);
            return *this;
        }

        constexpr bool operator<(_Enum e) const
        {
            return _enum < e;
        }

        constexpr bool operator>(_Enum e) const
        {
            return _enum > e;
        }

        constexpr bool operator==(_Enum e) const
        {
            return _enum == e;
        }

        constexpr bool operator!=(_Enum e) const
        {
            return _enum != e;
        }

        constexpr Type begin() const
        {
            return _Enum::MIN;
        }

        constexpr Type end() const
        {
            return _Enum::MAX;
        }

        constexpr Type &operator*()
        {
            return *this;
        }

        _Enum _enum;
    };

}
