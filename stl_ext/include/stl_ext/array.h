/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <array>
#include <cstddef>
#include <stdint.h>
#include <utility>

namespace stdex {

    // std::array of type _Ta from the arguments
    //
    // static constexpr auto kPins = stdex::array_of<const uint8_t>(1, 2, 3);
    template <typename _Ta, typename... _Tb>
    constexpr auto array_of(_Tb&&... t) -> std::array<_Ta, sizeof...(_Tb)>
    {
        return {
            {
                static_cast<_Ta>(std::forward<_Tb>(t))...
            }
        };
    }

    //
    // constexpr string arrays
    //
    // stdex::array_concat(stdex::str_to_array("Hello "), stdex::int_to_array<int, NUMBER>(), stdex::str_to_array("!"))
    //
    // ARRAY_F() stores the result as string in flash
    //
    // Serial.println(ARRAY_F(stdex::array_concat(stdex::int_to_array<int, kNumChannels>(), stdex::str_to_array(" Channels"))));
    //
#if ESP8266
    #define ARRAY_PSTRN(s, n)   (__extension__({static constexpr auto strArray __attribute__((__aligned__(n))) __attribute__((section( "\".irom0.pstr." __FILE__ "." __STRINGIZE(__LINE__) "."  __STRINGIZE(__COUNTER__) "\", \"aSM\", @progbits, 1 #"))) = (stdex::array_concat(s, stdex::char_to_array<0>())); strArray.data();}))
#else
    #define ARRAY_PSTRN(s, n)   (__extension__({static constexpr auto strArray __attribute__((__aligned__(n))) = (stdex::array_concat(s, stdex::char_to_array<0>())); strArray.data();}))
#endif
    #define ARRAY_PSTR(s)       ARRAY_PSTRN(s, 1)
    #define ARRAY_F(s)          FPSTR(ARRAY_PSTR(s))

    // number of digits of N including the sign
    template <typename T, T N, size_t BASE>
    constexpr size_t count_digits()
    {
        if (N == 0) {
            return 1;
        }
        size_t res = 0;
        T n = N;
        if (n < 0) {
            ++res;
        }
        for (; n; n /= static_cast<T>(BASE)) {
            ++res;
        }
        return res;
    }

    template <typename T, T N>
    constexpr auto int_to_array()
    {
        constexpr auto digit_count = count_digits<T, N, 10>();
        std::array<char, digit_count> res{};

        T n = N;
        size_t pos = digit_count;
        do {
            auto digit = n % 10;
            res[--pos] = static_cast<char>('0' + (digit < 0 ? -digit : digit));
            n /= 10;
        } while (n);
        if (N < 0) {
            res[0] = '-';
        }
        return res;
    }

    template <size_t N>
    constexpr std::array<char, N - 1> str_to_array(const char (&a)[N])
    {
        std::array<char, N - 1> res{};
        for (size_t i = 0; i != N - 1; ++i) {
            res[i] = a[i];
        }
        return res;
    }

    template<char N>
    constexpr auto char_to_array()
    {
        return std::array<char, 1>({N});
    }

    template <size_t ...Ns>
    constexpr std::array<char, (Ns + ...)> array_concat(const std::array<char, Ns>&... as)
    {
        std::array<char, (Ns + ...)> res{};
        size_t i = 0;
        auto l = [&](const auto& a) { for (auto c : a) {res[i++] = c;} };

        (l(as), ...);
        return res;
    }

}
