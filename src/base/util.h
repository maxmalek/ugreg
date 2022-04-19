#pragma once

#include "types.h"

// Clang has this, other compilers may or may not
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif


namespace detail
{
    template <typename T, size_t N>
    char(&_ArraySizeHelper(T(&a)[N]))[N];
}
#define Countof(a) (sizeof(detail::_ArraySizeHelper(a)))


struct NumConvertResult
{
    size_t used; // number of chars processed
    bool overflow;

    // this is probably what you want to check for
    inline bool ok() const { return used && !overflow; }
};

// for not necessarily \0-terminated strings. always writes dst.
// pass maxlen == -1 to stop at \0, otherwise after maxlen chars.
NumConvertResult strtosizeNN(size_t *dst, const char *s, size_t maxlen = -1);
NumConvertResult strtou64NN(u64* dst, const char* s, size_t maxlen = -1);
NumConvertResult strtoi64NN(s64* dst, const char* s, size_t maxlen = -1);

// converts a time value to milliseconds and stores in dst.
// accepted suffixes: d, h, m, s, ms
// so something like 2h30m5s is valid. spaces are invalid.
// s may or may not be \0-terminated.
NumConvertResult strToDurationMS_NN(u64 *dst, const char *s, size_t maxlen = -1);

// safe version that checks that exactly maxlen bytes were consumed
// (or if -1, that the end of the string was hit).
bool strToDurationMS_Safe(u64* dst, const char* s, size_t maxlen = -1);


u64 timeNowMS();

unsigned getNumCPUCores();

// Sleep calling thread for some amount of ms.
// Returns how long the sleep actually took (to account for variations in scheduling, etc)
u64 sleepMS(u64 ms);

u32 strhash(const char *s);
u32 roundPow2(u32 v);

char *sizetostr_unsafe(char *buf, size_t bufsz, size_t num);

template<typename T>
inline bool add_check_overflow(T* res, T a, T b)
{
#if __has_builtin(__builtin_add_overflow)
    return __builtin_add_overflow(a, b, res);
#else
    return ((*res = a + b)) < a;
#endif
}

template<typename T>
inline bool mul_check_overflow(T* res, T a, T b)
{
#if __has_builtin(__builtin_mul_overflow)
    return __builtin_mul_overflow(a, b, res);
#else
    T tmp = a * b;
    *res = tmp;
    return a && tmp / a != b;
#endif
}

size_t base64size(size_t len);
size_t base64enc(char* dst, const unsigned char* src, size_t src_len);
size_t base64dec(char* dst, size_t* dst_len, const unsigned char* src, size_t src_len);
