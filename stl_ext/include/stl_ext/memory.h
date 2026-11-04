/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <new>

namespace stdex {

    // deletes the object and sets the pointer to nullptr
    template <typename _Ta>
    void reset(_Ta &object)
    {
        if (object) {
            delete object;
            object = nullptr;
        }
    }

    // deletes the object and assigns the new one
    template <typename _Ta, typename _Tb>
    void reset(_Ta &object, const _Tb &new_object)
    {
        if (object) {
            delete object;
        }
        object = new_object;
    }

    //
    // storage for an object that is not initialized at startup (for example in a .noinit section)
    // ctor() must be called for each object to create it in the reserved memory
    //
    template<typename _T>
    union __attribute__((aligned(4))) UninitializedClass {
        using value_type = _T;

        static constexpr size_t kDWordSize = ((sizeof(value_type) + 3) >> 2);
        static constexpr size_t kPaddedSize = kDWordSize << 2;

        uint32_t _buffer[kDWordSize];
        value_type _object;

        UninitializedClass() {}
        ~UninitializedClass() {}

        template<class... _Args>
        __attribute__((__always_inline__))
        inline void ctor(_Args... args)
        {
            ::new(static_cast<void *>(&_object)) value_type(args...);
        }
    };

}
