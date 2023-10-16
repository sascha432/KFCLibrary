/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include "../stl_ext.h"
#include "./type_traits.h"
#include <stdlib.h>

namespace STL_STD_EXT_NAMESPACE {

#if __HAS_CPP17 != 1 && __HAVE_CPP14 == 0 && _MSC_VER == 0

    template<class T, class U = T>
    T exchange(T& obj, U&& new_value)
    {
        T old_value = move(obj);
        obj = forward<U>(new_value);
        return old_value;
    }

#endif

}
namespace STL_STD_EXT_NAMESPACE_EX {

    inline static int randint(int from, int to) {
        return from + (rand() % (to - from));
    }

    template<typename _Ta, typename _Tb, typename _Tret = std::common_type_t<std::make_unsigned_t<_Ta>, std::make_unsigned_t<_Tb>>>
    constexpr _Tret max_unsigned(_Ta a, _Tb b) {
        return max(static_cast<_Tret>(b), static_cast<_Tret>(b));
    }

    template<typename _Ta, typename _Tb, typename _Tret = std::common_type_t<std::make_signed_t<_Ta>, std::make_signed_t<_Tb>>>
    constexpr _Tret max_signed(_Ta a, _Tb b) {
        return max(static_cast<_Tret>(b), static_cast<_Tret>(b));
    }

    template<typename _Ta, typename _Tb, typename _Tret = std::common_type_t<std::make_unsigned_t<_Ta>, std::make_unsigned_t<_Tb>>>
    constexpr _Tret min_unsigned(_Ta a, _Tb b) {
        return min(static_cast<_Tret>(b), static_cast<_Tret>(b));
    }

    template<typename _Ta, typename _Tb, typename _Tret = std::common_type_t<std::make_signed_t<_Ta>, std::make_signed_t<_Tb>>>
    constexpr _Tret min_signed(_Ta a, _Tb b) {
        return min(static_cast<_Tret>(b), static_cast<_Tret>(b));
    }

    // creates an object on the stack and returns a reference to it
    // the object gets destroyed when the function returns
    //
    //  void myFunc(String &str) {
    //      str += "456";
    //      Serial.println(str);
    //  }
    //
    //  myFunc(&std::stack_reference<String>(123));
    //
    // equal to
    //
    //  {
    //      String tmp(123);
    //      myFunc(tmp);
    //  }

    template<typename _Ta>
    class stack_reference {
    public:
        template<typename... Args>
        stack_reference(Args &&... args) : _object(std::forward<Args>(args)...) {
        }
        _Ta &get() {
            return _object;
        }
        const _Ta &get() const {
            return _object;
        }
        _Ta &operator&() {
            return _object;
        }
        const _Ta &operator&() const {
            return _object;
        }
    private:
        _Ta _object;
    };

    template<typename _Enum>
    struct enum_type {
        using Type = enum_type<_Enum>;
        using Enum = _Enum;

        constexpr enum_type() : _enum(static_cast<_Enum>(0))  {
        }

        constexpr enum_type(_Enum e) : _enum(e) {
        }

        constexpr operator _Enum() const {
            return _enum;
        }

        constexpr Type operator|(Type e) {
            return static_cast<_Enum>(std::to_underlying<_Enum>(_enum) | std::to_underlying<_Enum>(e._enum));
        }

        // return *this & ~Type(e);
        constexpr Type operator^(Type e) {
            return static_cast<_Enum>(std::to_underlying<_Enum>(_enum) & ~std::to_underlying<_Enum>(e._enum));
        }

        constexpr operator bool() {
            return std::to_underlying<_Enum>(_enum) != 0;
        }

        constexpr Type operator&(Type e) {
            return static_cast<const _Enum>(std::to_underlying<_Enum>(_enum) & std::to_underlying<_Enum>(e._enum));
        }

        constexpr Type operator~() {
            return static_cast<const _Enum>(~std::to_underlying<_Enum>(_enum));
        }

        constexpr Type &operator|=(Type e) {
            _enum = (static_cast<_Enum>(std::to_underlying<_Enum>(_enum) | std::to_underlying<_Enum>(e._enum)));
            return *this;
        }

        constexpr Type &operator&=(Type e) {
            _enum = (static_cast<_Enum>(std::to_underlying<_Enum>(_enum) & std::to_underlying<_Enum>(e._enum)));
            return *this;
        }

        // remove bits from e from this
        // *this &= ~Type(e);
        constexpr Type &operator^=(Type e) {
            _enum = (static_cast<_Enum>(std::to_underlying<_Enum>(_enum) & ~std::to_underlying<_Enum>(e._enum)));
            return *this;
        }

        //
        // range methods
        //
        // for(auto type: enum_type<class Enum>) {}
        // iterated from Enum::MIN to Enum::MAX
        //
        // for(auto type = enum_type(); type != type.end(); ++type) {}
        //

        constexpr Type &operator++() {
            _enum = static_cast<_Enum>(std::to_underlying<_Enum>(_enum) + 1);
            return *this;
        }

        constexpr Type &operator--() {
            _enum = static_cast<_Enum>(std::to_underlying<_Enum>(_enum) - 1);
            return *this;
        }

        constexpr Type operator++(int value) {
            auto tmp = *this;
            _enum = static_cast<_Enum>(std::to_underlying<_Enum>(_enum) + value);
            return tmp;
        }

        constexpr Type operator--(int value) {
            auto tmp = *this;
            _enum = static_cast<_Enum>(std::to_underlying<_Enum>(_enum) - value);
            return tmp;
        }

        constexpr void operator=(_Enum e) {
            _enum = e;
        }

        constexpr void operator=(Type e) {
            *this = e;
        }

        constexpr bool operator<(_Enum e) const {
            return _enum < e;
        }

        constexpr bool operator>(_Enum e) const {
            return _enum > e;
        }

        constexpr bool operator==(_Enum e) const {
            return _enum == e;
        }

        constexpr bool operator!=(_Enum e) const {
            return _enum != e;
        }

        constexpr Type begin() {
            return _Enum::MIN;
        }

        constexpr Type begin() const {
            return _Enum::MIN;
        }

        constexpr Type end() {
            return _Enum::MAX;
        }

        constexpr Type end() const {
            return _Enum::MAX;
        }

        constexpr Type &operator *() {
            return *this;
        }

        _Enum _enum;
    };

}
