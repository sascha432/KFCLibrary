/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include "../stl_ext.h"
#include "./type_traits.h"
#include <array>
#include <cstddef>
#include <cassert>

namespace STL_STD_EXT_NAMESPACE {

    template <typename _Ta, typename... _Tb>
    constexpr auto array_of(_Tb&&... t) -> std::array <_Ta, sizeof...(_Tb)> {
        return {
            {
                static_cast<_Ta>(std::forward<_Tb>(t))...
            }
        };
    }

    // copy initializer list into target without checking target size

    // uint8_t test2[4];
    // uint8_t *test = &test2[0];

    // std::copy_array<uint8_t>({ 1, 2, 3, 4 }, test2);
    // std::copy_array<uint8_t>({ 1, 2, 3, 4 }, test);

    template<typename _Ta>
    struct copy_array {
        using element_type = std::remove_pointer_t<std::remove_extent_t<_Ta>>;
        copy_array(std::initializer_list<const element_type> l, element_type *target) {
            std::copy(l.begin(), l.end(), target);
        }
    };

}


namespace STL_STD_EXT_NAMESPACE_EX {

    template <typename _Ta, typename... _Tb>
    constexpr auto array_of(_Tb&&... t) -> std::array <_Ta, sizeof...(_Tb)> {
        return {
            {
                static_cast<_Ta>(std::forward<_Tb>(t))...
            }
        };
    }

    template<typename _Ta>
    using copy_array = STL_STD_EXT_NAMESPACE::copy_array<_Ta>;



    // constexpr array iterator

#if !defined(__clang__) && __GNUC__ < 5
# define ce_assert(x) ((void)0)
#else
# define ce_assert(x) ((void)0)
// # define ce_assert(x) assert(x)
#endif
    namespace cexpr {

        template <class It, class T>
        inline constexpr It
            find(It begin, It end, T const &value) noexcept
        {
            return !(begin != end && *begin != value)
                ? begin
                : find(begin + 1, end, value);
        }

        template<class Array>
        class array_iterator
        {
        public:
            using value_type = typename Array::value_type;

            constexpr array_iterator(const Array &array, size_t size = 0u) noexcept
                : array_(&array)
                , pos_(size)
            {}

            constexpr const value_type operator* () const noexcept
            {
                return ce_assert(pos_ < Array::size), (*array_)[pos_];
            }

#if __HAS_CPP14
            constexpr
#endif
                array_iterator &operator ++() noexcept
            {
                return ce_assert(pos_ < Array::size), ++pos_, *this;
            }

            constexpr array_iterator operator+ (size_t n) const noexcept
            {
                return ce_assert(pos_ + n <= Array::size), array_iterator(*array_, pos_ + n);
            }

            friend constexpr bool
                operator != (const array_iterator &i1, const array_iterator &i2) noexcept
            {
                return i1.array_ != i2.array_ || i1.pos_ != i2.pos_;
            }

            friend constexpr size_t
                operator- (array_iterator const &i1, array_iterator const &i2) noexcept
            {
                return ce_assert(i1.array_ == i2.array_), i1.pos_ - i2.pos_;
            }

        private:
            const Array *array_;
            size_t pos_;
        };

        template<typename T, size_t N>
        class array
        {
        public:
            using value_type = T;
            using const_iterator = const array_iterator<array>;

            constexpr value_type const &
                operator[] (size_t idx) const noexcept
            {
                return array_[idx];
            }

            constexpr const_iterator begin() const noexcept
            {
                return const_iterator(*this);
            }

            constexpr const_iterator end()   const noexcept
            {
                return const_iterator(*this, N);
            }

            T array_[N];
            static constexpr size_t size = N;
        };

        template <typename _Ta, typename... _Tb>
        constexpr auto array_of(_Tb&&... t) -> std::array <_Ta, sizeof...(_Tb)> {
            return {
                {
                    static_cast<_Ta>(std::forward<_Tb>(t))...
                }
            };
        }


    }

    // array sequence maker

    template<typename _Type>
    struct ArrayFormatter {
        using type = _Type;
        static constexpr _Type format(_Type index) {
            return index;
        };
    };

    using IntArrayFormatter = ArrayFormatter<int>;
    using UInt8ArrayFormatter = ArrayFormatter<uint8_t>;


    template<typename _Formatter, int N, int... Rest>
    struct Array_impl {
        static constexpr auto &value = Array_impl<_Formatter, N - 1, _Formatter::format(N), Rest...>::value;
    };

    template<typename _Formatter, int... Rest>
    struct Array_impl<_Formatter, 0, Rest...> {
        static constexpr typename _Formatter::type value[] = { 0, Rest... };
    };

    template<typename _Formatter, int... Rest>
    constexpr typename _Formatter::type Array_impl<_Formatter, 0, Rest...>::value[];


    template<int _Size, typename _Formatter = IntArrayFormatter>
    struct Array {
        static_assert(_Size >= 0, "N must be at least 0");

        static constexpr auto &value = Array_impl<_Formatter, _Size>::value;

        Array() = delete;
        Array(const Array &) = delete;
        Array(Array &&) = delete;
    };

    // static constexpr auto ExampleArrayInt = Array<16>::value;
    // static constexpr auto ExampleArrayUint8 = Array<16, UInt8ArrayFormatter>::value;

#if __HAS_CPP17

    // constexpr array/string functions

    #define ARRAY_PSTRN(s,n)    (__extension__({static constexpr auto strArray __attribute__((__aligned__(n))) __attribute__((section( "\".irom0.pstr." __FILE__ "." __STRINGIZE(__LINE__) "."  __STRINGIZE(__COUNTER__) "\", \"aSM\", @progbits, 1 #"))) = (stdex::array_concat(s, stdex::char_to_array<0>())); &strArray.data()[0];}))
    #define ARRAY_PSTR(s)       ARRAY_PSTRN(s, 1)
    #define ARRAY_F(s)          FPSTR(ARRAY_PSTR(s))

    // // example
    // constexpr int NUMBER = -42;
    // Serial.println(ARRAY_F(stdex::array_concat(stdex::str_to_array("Hello "), stdex::int_to_array<int, NUMBER>(), stdex::str_to_array("!"))));

    template <typename T, T N, size_t BASE>
    constexpr std::size_t __count_digit() {
        if (N == 0) {
            return 1;
        }
        std::size_t res = 0;
        if (N < 0) {
            ++res;
        }
        for (int i = N < 0 ? -N : N; i; i /= BASE) {
            ++res;
        }
        return res;
    }

    template <typename T, T N>
    constexpr auto int_to_array()
    {
        constexpr auto digit_count = __count_digit<T, N, 10>();
        std::array<char, digit_count> res{};

        auto n = N;
        auto d = digit_count;
        if (n < 0) {
            n = -n;
            res[0] = '-';
            --d;
        }
        for (std::size_t i = 0; i != d; ++i) {
            res[digit_count - 1 - i] = static_cast<char>('0' + n % 10);
            n /= 10;
        }
        return res;
    }

    template<uintmax_t N, bool UPPERCASE = false>
    constexpr auto hex_to_array()
    {
        constexpr auto digit_count = stdex::__count_digit<uintmax_t, N, 16>();
        std::array<char, digit_count> res{};

        auto n = N;
        for (std::size_t i = 0; i != digit_count; ++i) {
            auto ch = n % 16;
            if (ch > 9) {
                ch += (UPPERCASE ? 'A' : 'a') - 10;
            }
            else {
                ch += '0';
            }
            res[digit_count - 1 - i] = static_cast<char>(ch);
            n /= 16;
        }
        return res;
    }

    template <std::size_t N>
    constexpr std::array<char, N - 1> str_to_array(const char (&a)[N])
    {
        std::array<char, N - 1> res{};
        for (std::size_t i = 0; i != N - 1; ++i) {
            res[i] = a[i];
        }
        return res;
    }

    template<char N>
    constexpr auto char_to_array()
    {
        return std::array<char, 1>({N});
    }

    template <std::size_t ...Ns>
    constexpr std::array<char, (Ns + ...)> array_concat(const std::array<char, Ns>&... as)
    {
        std::array<char, (Ns + ...)> res{};
        std::size_t i = 0;
        auto l = [&](const auto& a) { for (auto c : a) {res[i++] = c;} };

        (l(as), ...);
        return res;
    }

#endif

}

