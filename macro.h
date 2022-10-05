#pragma once

#define _unlikely_(x) __builtin_expect(!!(x), 0)
#define _cleanup_(x) __attribute__((cleanup(x)))
#define min(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })
#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })
#define TAKE_PTR(ptr)                     \
    ({                                    \
        __typeof__(ptr) *_pptr_ = &(ptr); \
        __typeof__(ptr) _ptr_ = *_pptr_;  \
        *_pptr_ = NULL;                   \
        _ptr_;                            \
    })
