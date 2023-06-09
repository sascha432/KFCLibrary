/**
 * Author: sascha_lammers@gmx.de
 */

#pragma once

#include "../stl_ext.h"
#include <memory>
#include "./non_std.h"

namespace STL_STD_EXT_NAMESPACE_EX {

    // std::vector<std:unique_ptr<Test>> _timers;
    // Test *timer;
    // auto iterator = std::find_if(_timers.begin(), _timers.end(), stdex::compare_unique_ptr(timer));

#pragma push_macro("new")
#undef new

    // create an object inside any writable and executable memory area
    template<typename _Ta, class... _Args>
    inline static void new_at(_Ta *ptr, _Args... args)
    {
        ::new(static_cast<void *>(ptr)) _Ta(args...);
    }

    template<typename _Ta, class... _Args>
    inline static void new_at(_Ta &obj, _Args... args)
    {
        ::new(static_cast<void *>(std::addressof(obj))) _Ta(args...);

    }

#pragma pop_macro("new")

    template <typename _Ta>
    void reset(_Ta &object) {
        if (object) {
            delete object;
            object = nullptr;
        }
    }

    template <typename _Ta, typename _Tb>
    void reset(_Ta &object, const _Tb &new_object) {
        if (object) {
            delete object;
        }
        object = new_object;
    }

    template <typename _Ta>
    class compare_unique_ptr_function : public non_std::unary_function<_Ta, bool>
    {
    protected:
        _Ta *_ptr;
    public:
        explicit compare_unique_ptr_function(_Ta *ptr) : _ptr(ptr) {}
        bool operator() (const std::unique_ptr<_Ta> &obj) const {
            return obj.get() == _ptr;
        }
    };

    template <typename _Ta>
    static inline compare_unique_ptr_function<_Ta> compare_unique_ptr(_Ta *ptr) {
        return compare_unique_ptr_function<_Ta>(ptr);
    }

    template<typename _T>
    union __attribute__((aligned(4))) UninitializedClass {
        using value_type = _T;

        static constexpr size_t kDWordSize = ((sizeof(value_type) + 3) >> 2);
        static constexpr size_t kPaddedSize = kDWordSize << 2;

        uint32_t _buffer[kDWordSize];
        value_type _object;

        // the constructor will be called after init()
        UninitializedClass() {}
        ~UninitializedClass() {}

        // ctor() must be called for each uninitialized object
        // it creates the object in the reserved memory calling the c'tor with the arguments
        template<class... _Args>
        __attribute__((__always_inline__))
        inline void ctor(_Args... args)
        {
            new_at(&_object, args...);
        }

    };

}

#if __HAS_CPP14 == 0

namespace STL_STD_EXT_NAMESPACE {

    template<class _Ta, class... _Args>
    std::enable_if_t<!std::is_array<_Ta>::value, std::unique_ptr<_Ta>>
    make_unique(_Args&&... args)
    {
        return std::unique_ptr<_Ta>(new _Ta(std::forward<_Args>(args)...));
    }

    template<class _Ta>
    std::enable_if_t<is_unbounded_array<_Ta>::value, std::unique_ptr<_Ta>>
    make_unique(std::size_t n)
    {
        return std::unique_ptr<_Ta>(new std::remove_extent_t<_Ta>[n]());
    }

    template<class _Ta, class... _Args>
    std::enable_if_t<is_bounded_array<_Ta>::value> make_unique(_Args&&...) = delete;

}

#endif
