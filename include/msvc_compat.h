#ifndef QTNETWORKCHAT_MSVC_COMPAT_H
#define QTNETWORKCHAT_MSVC_COMPAT_H

#ifdef _MSC_VER
#include <cstddef>

// Qt 5.15.2's qcompilerdetection.h unconditionally uses stdext::make_checked_array_iterator
// and stdext::make_unchecked_array_iterator. These helpers were removed from newer MSVC STL
// (VS 2022+). Provide a minimal shim so Qt 5 builds keep working with newer compilers.
namespace stdext {
    template <typename T>
    inline T make_checked_array_iterator(T ptr, std::size_t) { return ptr; }

    template <typename T>
    inline T make_unchecked_array_iterator(T ptr) { return ptr; }
}

#endif // _MSC_VER

#endif // QTNETWORKCHAT_MSVC_COMPAT_H
