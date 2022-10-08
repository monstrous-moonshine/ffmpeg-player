#pragma once

#define DEFAULT_FRAME_DELAY 16
#define MAX_BUFFER_SIZE (32 * 1024)

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
#define LOG_ERROR(fmt, ...) \
    ({ \
        fprintf(stderr, "\e[31m"); \
        fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __FUNCTION__); \
        fprintf(stderr, fmt, ## __VA_ARGS__); \
        fprintf(stderr, "\e[0m"); \
        (void)0; \
    })
