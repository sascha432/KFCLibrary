/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <type_traits>

namespace stdex {

    //
    // type of the member a member pointer points to
    //
    template<typename _Ta>
    struct member_pointer_value;

    template<typename _Class, typename _Value>
    struct member_pointer_value<_Value _Class::*> {
        using type = _Value;
    };

    template<typename _Ta>
    using member_pointer_value_t = typename member_pointer_value<_Ta>::type;

    //
    // underlying type for an enum or T if it is not an enum
    //
    template<typename _Ta, bool = std::is_enum<_Ta>::value>
    struct relaxed_underlying_type {
        using type = std::underlying_type_t<_Ta>;
    };

    template<typename _Ta>
    struct relaxed_underlying_type<_Ta, false> {
        using type = _Ta;
    };

    template<typename _Ta>
    using relaxed_underlying_type_t = typename relaxed_underlying_type<_Ta>::type;

    //
    // true if the type is a "C" string: char *, char[], const char *, const char[]
    //
    template<typename _Ta>
    struct is_c_str : std::is_same<char *,
        std::conditional_t<
            std::is_pointer<_Ta>::value || std::is_reference<_Ta>::value || std::is_array<_Ta>::value,
            std::add_pointer_t<std::remove_cv_t<std::remove_extent_t<std::remove_pointer_t<std::remove_reference_t<_Ta>>>>>,
            _Ta
        >
    > {
    };

    //
    // value of an enum as its underlying type (std::to_underlying of C++23)
    //
    template<typename _Enum>
    constexpr std::underlying_type_t<_Enum> to_underlying(_Enum value) noexcept
    {
        return static_cast<std::underlying_type_t<_Enum>>(value);
    }

}
