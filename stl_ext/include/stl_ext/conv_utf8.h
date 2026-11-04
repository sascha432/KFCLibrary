/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <array>
#include <iterator>

#ifndef STL_STD_EXT_CONV_UTF8_ASSERT
#define STL_STD_EXT_CONV_UTF8_ASSERT 0
#endif

#if STL_STD_EXT_CONV_UTF8_ASSERT
#include <assert.h>
#define STL_EXT_CONV_UTF8_ASSERT(...) assert(__VA_ARGS__)
#else
#define STL_EXT_CONV_UTF8_ASSERT(...)
#endif

#pragma GCC push_options
#pragma GCC optimize ("O2")

//
// validate and copy UTF-8 strings, invalid sequences are replaced or removed
//
// size_t len = strlen(src);
// size_t dst_len = len;
// stdex::conv::utf8::strlen<stdex::conv::utf8::DefaultReplacement>(src, dst_len, len);     // dst_len = bytes required
// stdex::conv::utf8::strcpy<stdex::conv::utf8::DefaultReplacement>(dst, src, dst_len, len);
//

namespace stdex {

    namespace conv {

        struct Unicode {

            // number of bytes of the UTF-8 encoding, 0 for an invalid code point
            static constexpr size_t getLengthFromUnicode(uint32_t value)
            {
                return value <= 0x7f ? 1 : value <= 0x7ff ? 2 : value <= 0xffff ? 3 : value <= 0x10ffff ? 4 : 0;
            }

            // UTF-8 encoding of value with N bytes
            template<size_t N>
            static constexpr std::array<uint8_t, N> encodeUTF8(uint32_t value)
            {
                std::array<uint8_t, N> bytes{};
                if constexpr (N == 1) {
                    bytes[0] = static_cast<uint8_t>(value);
                }
                else if constexpr (N > 1) {
                    // lead byte 110xxxxx, 1110xxxx or 11110xxx
                    bytes[0] = static_cast<uint8_t>(((0xff00U >> N) & 0xffU) | (value >> (6 * (N - 1))));
                    // continuation bytes 10xxxxxx
                    for (size_t i = 1; i < N; i++) {
                        bytes[i] = static_cast<uint8_t>(0x80U | ((value >> (6 * (N - 1 - i))) & 0x3fU));
                    }
                }
                return bytes;
            }

            template<uint32_t _Unicode>
            struct toUTF8 {
                static constexpr uint32_t unicode_value = _Unicode;
                static constexpr size_t kLength = getLengthFromUnicode(_Unicode);
                static constexpr std::array<uint8_t, kLength> value = encodeUTF8<kLength>(_Unicode);

                static constexpr size_t size() {
                    return kLength;
                }
            };

        };

        struct utf8 {

            struct RemoveInvalidEncoding : Unicode::toUTF8<~0U> {
            };

            template<uint32_t _Unicode>
            struct ReplaceInvalidEncodingUnicodeSymbol : Unicode::toUTF8<_Unicode> {
                static_assert(_Unicode >= 0x20 && _Unicode < 0xffff, "Unicode is limit to 0x0020 - 0xffff");
            };

            template<uint32_t _Ascii>
            struct ReplaceInvalidEncodingAsciiSymbol : Unicode::toUTF8<_Ascii> {
                static_assert(_Ascii >= 0x20 && _Ascii < 0x80, "ASCII symbols are limited to 0x20 - 0x7f");
            };

            using QuestionMarkReplacement = Unicode::toUTF8<'?'>;

            // U+FFFD
            struct DefaultReplacement : Unicode::toUTF8<0xfffd> {
            };

            //
            // helper returns
            // 0            for success, src/dst have been moved to the next character
            // not 0        for invalid character or end of dst/src reached
            //              src points to the next character, dst is unchanged

            struct __strcpy_helper {

                //
                // append 0 byte
                //
                template<typename _Iter>
                inline static _Iter end(_Iter dst)
                {
                    *dst = 0;
                    return dst;
                }

                //
                // copy single byte
                //
                template <typename _Iter1, typename _Iter2>
                inline static int copy_one(_Iter1 &dst, _Iter1 dst_end, _Iter2 &src, _Iter2 src_end)
                {
                    //return func(dst, dst_end, src, src_end, (uint8_t)len);
                    STL_EXT_CONV_UTF8_ASSERT(dst < dst_end);
                    STL_EXT_CONV_UTF8_ASSERT(src < src_end);
                    *dst = *src;
                    ++dst;
                    ++src;
                    return 0;
                }

                 //
                // validate utf8 sequence and copy valid data
                //
                template <typename _Iter1, typename _Iter2>
                inline static int copy(_Iter1 &dst, _Iter1 dst_end, _Iter2 &src, _Iter2 src_end, uint8_t len)
                {
                    STL_EXT_CONV_UTF8_ASSERT(dst < dst_end);
                    STL_EXT_CONV_UTF8_ASSERT(src < src_end);
                    auto in = src + 1;
                    auto out = dst + 1;
                    while (--len && out < dst_end && in < src_end && (static_cast<uint8_t>(*in) & 0xc0) == 0x80) {
                        ++in;
                        ++out;
                    }
                    if (len == 0) {
                        // copy validated sequence
                        while (src < in) {
                            *dst = *src;
                            ++dst;
                            ++src;
                        }
                        return 0;
                    }
                    else {
                        // point to the next character after the invalid sequence
                        src = in + len;
                    }
                    return len;
                }
            };

            struct __strlen_helper {

                //
                // append 0 byte
                //
                template<typename _Iter>
                inline static _Iter end(_Iter dst)
                {
                    return dst;
                }

                //
                // copy single byte
                //
                template <typename _Iter1, typename _Iter2>
                inline static int copy_one(_Iter1 &dst, _Iter1 dst_end, _Iter2 &src, _Iter2 src_end)
                {
                    STL_EXT_CONV_UTF8_ASSERT(dst < dst_end);
                    STL_EXT_CONV_UTF8_ASSERT(src < src_end);
                    ++dst;
                    ++src;
                    return 0;
                }

                //
               // validate utf8 sequence and copy valid data
               //
                template <typename _Iter1, typename _Iter2>
                inline static int copy(_Iter1 &dst, _Iter1 dst_end, _Iter2 &src, _Iter2 src_end, uint8_t len)
                {
                    STL_EXT_CONV_UTF8_ASSERT(dst < dst_end);
                    STL_EXT_CONV_UTF8_ASSERT(src < src_end);
                    auto in = src + 1;
                    auto out = dst + 1;
                    while (--len && out < dst_end && in < src_end && (static_cast<uint8_t>(*in) & 0xc0) == 0x80) {
                        ++in;
                        ++out;
                    }
                    if (len == 0) {
                        src = in;
                        dst = out;
                        return 0;
                    }
                    else {
                        // point to the next character after the invalid sequence
                        src = in + len;
                    }
                    return len;
                }
            };

            // template for reading, validating and copying utf8 strings
            //
            // _SymbolType      type that represents the symbol for invalid utf8 sequences
            //                  stdex::conv::utf8::DefaultReplacement = U+FFFD
            //                  stdex::conv::utf8::QuestionMarkReplacement = ?
            //                  stdex::conv::utf8::RemoveInvalidEncoding
            //                  stdex::conv::utf8::ReplaceInvalidEncodingAsciiSymbol<'.'> = .
            //                  stdex::conv::utf8::ReplaceInvalidEncodingUnicodeSymbol<0x00bf> = U+00BF
            //
            // _Fn              type with statics methods to read/write data
            //
            // returns          length of utf8 characters written
            //                  _dst_end is set to the end of the data written
            //
            template <typename _SymbolType, typename _Fn, typename _Iter1, typename _Iter2>
            static size_t __validate_utf8_encoding(_Iter1 dst_begin, _Iter1 &_dst_end, const _Iter2 &_src_begin, _Iter2 src_end)
            {
                auto src_begin = _src_begin;
                auto dst_end = std::prev(_dst_end);
                size_t out_len = 0;

                while (src_begin < src_end && dst_begin < dst_end) {
                    int result;
                    auto code1 = static_cast<uint8_t>(*src_begin);
                    if (code1 < 0x80) {
                        result = _Fn::copy_one(dst_begin, dst_end, src_begin, src_end);
                    }
                    else {
                        uint8_t advance;
                        code1 >>= 3;
                        if (code1 == 0x1e) {
                            advance = 4;
                        }
                        else if ((code1 = (code1 >> 1)) == 0xe) {
                            advance = 3;
                        }
                        else if ((code1 = (code1 >> 1)) == 0x6) {
                            advance = 2;
                        }
                        else {
                            ++src_begin;
                            if constexpr (_SymbolType::size() != 0) {
                                result = 1;
                                goto checkSymbol;
                            }
                            continue;
                        }
                        result = _Fn::copy(dst_begin, dst_end, src_begin, src_end, advance);
                        if (result == 0) {
                            out_len++;
                            continue;
                        }

                        checkSymbol:
                        if constexpr (_SymbolType::size() != 0) {
                            // on failure, try to fit in the invalid symbol
                            if (!(dst_begin + _SymbolType::size() <= dst_end)) {
                                break;
                            }
                            const uint8_t *symbol_begin = _SymbolType::value.data();
                            result = _Fn::copy(dst_begin, dst_end, symbol_begin, symbol_begin + _SymbolType::size(), static_cast<uint8_t>(_SymbolType::size()));
                        }
                    }

                    if (result == 0) {
                        out_len++;
                        continue;
                    }
                    if (result == -1) {
                        break;
                    }

                }

                _dst_end = _Fn::end(dst_begin);
                return out_len;
            }

            template<typename _SymbolType>
            inline static size_t strcpy(char *dst, const char *str, size_t &dst_len, size_t src_len)
            {
                auto endPtr = dst + dst_len + 1;
                auto result = __validate_utf8_encoding<_SymbolType, __strcpy_helper>(dst, endPtr, str, str + src_len + 1);
                dst_len = std::distance(dst, endPtr);
                return result;
            }

            // returns number of utf8 characters
            //
            // dst_len is the destination buffer used for copying
            //
            template<typename _SymbolType>
            inline static size_t strlen(const char *str, size_t &dst_len, size_t src_len)
            {
                auto dst_begin = const_cast<char *>(str);
                auto dst_end = dst_begin + dst_len + 1;
                auto result = __validate_utf8_encoding<_SymbolType, __strlen_helper>(dst_begin, dst_end, str, str + src_len + 1);
                dst_len = std::distance(dst_begin, dst_end);
                return result;
            }
        };

    }

}

#pragma GCC pop_options

#undef STL_EXT_CONV_UTF8_ASSERT
