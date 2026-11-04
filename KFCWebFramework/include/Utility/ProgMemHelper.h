
/**
* Author: sascha_lammers@gmx.de
*/

#pragma once

#include <pgmspace.h>
#include <stddef.h>
#include <pp_tools.h>

// utility for creating arrays with variable names for forms in PROGMEM

namespace ProgMemHelper {

    constexpr size_t indexDigits(size_t value)
    {
        size_t digits = 1;
        for (; value >= 10; value /= 10) {
            digits++;
        }
        return digits;
    }

    // "<text><sep><from>" ... "<text><sep><to - 1>" in one block with a fixed stride, the compiler
    // creates the strings. The object must be static (the forms keep the pointers)
    template<size_t kTextSize, size_t kSepSize, size_t kFrom, size_t kTo>
    class IndexedNames {
    public:
        static_assert(kTo > kFrom, "empty name table");

        static constexpr size_t kCount = kTo - kFrom;
        static constexpr size_t kStride = (kTextSize - 1) + (kSepSize - 1) + indexDigits(kTo - 1) + 1;

        constexpr IndexedNames(const char (&text)[kTextSize], const char (&sep)[kSepSize]) :
            _data{}
        {
            for(size_t i = 0; i < kCount; i++) {
                size_t pos = i * kStride;
                for(size_t j = 0; j < kTextSize - 1; j++) {
                    _data[pos++] = text[j];
                }
                for(size_t j = 0; j < kSepSize - 1; j++) {
                    _data[pos++] = sep[j];
                }
                // the rest of the stride stays 0 and terminates the string
                const size_t value = kFrom + i;
                size_t num = value;
                for(size_t k = indexDigits(value); k; k--) {
                    _data[pos + k - 1] = static_cast<char>('0' + (num % 10));
                    num /= 10;
                }
            }
        }

        const char *operator[](size_t index) const {
            return &_data[index * kStride];
        }

        static constexpr size_t size() {
            return kCount;
        }

    private:
        char _data[kCount * kStride];
    };

}

// PROGMEM_DEF_LOCAL_VARNAME_ARRAY(_VAR_, id, 3)
// _VAR_id[0..2] == "id_0", "id_1", "id_2"
#define PROGMEM_DEF_LOCAL_VARNAME_ARRAY(prefix, name, num) \
    static constexpr ProgMemHelper::IndexedNames<sizeof(#name), sizeof("_"), 0, (num)> prefix##name PROGMEM = { #name, "_" };

// PROGMEM_DEF_LOCAL_VARNAMES(_VAR_, 2, aa, bb, cc)
// _VAR_aa[0..1] == "aa_0", "aa_1"
// _VAR_bb[0..1] == "bb_0", "bb_1"
// _VAR_cc[0..1] == "cc_0", "cc_1"
// num can be any constant expression, up to 16 names
#define PROGMEM_DEF_LOCAL_VARNAMES(prefix, num, ...)            KFC_PP_FOR_EACH_2(__PROGMEM_DEF_LOCAL_VARNAME, prefix, num, __VA_ARGS__)
#define __PROGMEM_DEF_LOCAL_VARNAME(prefix, num, name)          PROGMEM_DEF_LOCAL_VARNAME_ARRAY(prefix, name, num)

// helpers to get the variable names
// for PROGMEM_DEF_LOCAL_VARNAMES(_VAR_, 2, aa, bb)
// F_VAR(aa, 1) would return _VAR_aa[1] == (const __FlashStringHelper *)("aa_1")
#define F_VAR(name, num)                                        reinterpret_cast<const __FlashStringHelper *>(_VAR_##name[num])
#define PSTR_VAR(name, num)                                     (_VAR_##name[num])

// helper for creating local PSTR arrays
// PROGMEM_DEF_LOCAL_PSTR_ARRAY(my_list, PSTR("1"), PSTR("2"), PSTR("3"));
#define PROGMEM_DEF_LOCAL_PSTR_ARRAY(name, ...) \
    const char *name[] = { __VA_ARGS__ }; \
    static constexpr uint8_t k##name##Size = sizeof(name) / sizeof(name[0]);

// PROGMEM_DEF_LOCAL_PSTR_ARRAY_INDEXED(pinNames, 0, 17, "", GPIO);
// pinNames[0..17] == "GPIO0" ... "GPIO17", kpinNamesSize == 18
#define PROGMEM_DEF_LOCAL_PSTR_ARRAY_INDEXED(name, from, to, separator, text) \
    static constexpr ProgMemHelper::IndexedNames<sizeof(#text), sizeof(separator), (from), (to) + 1> name PROGMEM = { #text, separator }; \
    static constexpr uint8_t k##name##Size = name.size();
