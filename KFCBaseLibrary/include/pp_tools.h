/**
  Author: sascha_lammers@gmx.de
*/

#pragma once

// preprocessor helpers (replaces the few Boost.Preprocessor macros that were used)

// concatenate after expanding both arguments
#define KFC_PP_CAT(a, b)                                        KFC_PP_CAT_I(a, b)
#define KFC_PP_CAT_I(a, b)                                      a##b

// first argument of a variadic list
// KFC_PP_FIRST(1, 2, 3) -> 1
#define KFC_PP_FIRST(...)                                       KFC_PP_FIRST_I(__VA_ARGS__, )
#define KFC_PP_FIRST_I(first, ...)                              first

// number of arguments, 1 to 16
// KFC_PP_NARGS(a, b, c) -> 3
#define KFC_PP_NARGS(...)                                       KFC_PP_NARGS_I(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, )
#define KFC_PP_NARGS_I(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

// call macro(a, b, x) for each x of the variadic list, 1 to 16 arguments
// KFC_PP_FOR_EACH_2(M, p, n, x, y) -> M(p, n, x) M(p, n, y)
#define KFC_PP_FOR_EACH_2(macro, a, b, ...)                     KFC_PP_CAT(KFC_PP_FE2_, KFC_PP_NARGS(__VA_ARGS__))(macro, a, b, __VA_ARGS__)
#define KFC_PP_FE2_1(m, a, b, x)                                m(a, b, x)
#define KFC_PP_FE2_2(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_1(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_3(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_2(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_4(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_3(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_5(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_4(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_6(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_5(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_7(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_6(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_8(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_7(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_9(m, a, b, x, ...)                           m(a, b, x) KFC_PP_FE2_8(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_10(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_9(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_11(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_10(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_12(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_11(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_13(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_12(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_14(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_13(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_15(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_14(m, a, b, __VA_ARGS__)
#define KFC_PP_FE2_16(m, a, b, x, ...)                          m(a, b, x) KFC_PP_FE2_15(m, a, b, __VA_ARGS__)
