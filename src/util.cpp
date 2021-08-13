#include "util.h"
#include <chrono>
#include <assert.h>
#include <thread>

// Clang has this, other compilers may or may not
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif


template<typename T>
inline static bool add_check_overflow(T *res, T a, T b)
{
#if __has_builtin(__builtin_add_overflow)
    return __builtin_add_overflow(a, b, res);
#else
    return ((*res = a + b)) < a;
#endif
}

template<typename T>
inline static bool mul_check_overflow(T* res, T a, T b)
{
#if __has_builtin(__builtin_mul_overflow)
    return __builtin_mul_overflow(a, b, res);
#else
    T tmp = a * b;
    *res = tmp;
    return a && tmp / a != b;
#endif
}

NumConvertResult strtosizeNN(size_t* dst, const char* s, size_t len)
{
    NumConvertResult res { 0, false };
    size_t k = 0;
    if(len) do
    {
        unsigned char c = s[res.used++]; // increment here...
        if(c >= '0' && c <= '9')
        {
            res.overflow |= mul_check_overflow<size_t>(&k, k, 10);
            res.overflow |= add_check_overflow<size_t>(&k, k, c - '0');
        }
        else // not a numeric char
        {
            --res.used; // ... so that this can never be 0 and thus never underflow
            break;
        }
    }
    while(res.used < len);

    *dst = k;
    return res;
}

enum TimeInMS : u64
{
    DUR_MS = 1,
    DUR_S = 1000 * DUR_MS,
    DUR_M = 60 * DUR_S,
    DUR_H = 60 * DUR_M,
    DUR_D = 24 * DUR_H
};

NumConvertResult strToDurationMS_NN(u64* dst, const char* s, size_t maxlen)
{
    NumConvertResult res{ 0, false };
    u64 ms = 0;

    while(*s)
    {
        size_t k;
        const char *const beg = s;
        NumConvertResult r = strtosizeNN(&k, s, maxlen);
        res.used += r.used;
        s += r.used;
        maxlen -= r.used;
        u64 unit = DUR_MS;
        if(maxlen)
        {
            size_t skip = 1;
            switch(*s)
            {
                case 'h': unit = DUR_H; break;
                case 'm': unit = (maxlen > 1 && s[1] == 's') ? (((void)(++skip)),DUR_MS) : DUR_M; break;
                case 's': unit = DUR_S; break;
                case 'd': unit = DUR_D; break;
                default: s = beg; // fall through
                case 0: goto out; // reset back to beg in case parsing num+suffix failed
            }
            maxlen -= skip;
            s += skip;
            res.used += skip;
        }
        assert(unit); // unreachable
        res.overflow |= r.overflow | mul_check_overflow<u64>(&k, k, unit);
        ms += k;
    }
out:
    *dst = ms;
    return res;
}

bool strToDurationMS_Safe(u64* dst, const char* s, size_t maxlen)
{
    if(!s)
        return false;

    NumConvertResult r = strToDurationMS_NN(dst, s, maxlen);
    //                     unk len and at end of s?        consumed exact # bytes?
    return !r.overflow && ((maxlen == -1 && !s[r.used]) || r.used == maxlen);
}

u64 timeNowMS()
{
    auto now = std::chrono::steady_clock::now();
    auto t0 = now.time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t0);
    return ms.count();
}

unsigned getNumCPUCores()
{
    return std::thread::hardware_concurrency();
}

u64 sleepMS(u64 ms)
{
    u64 now = timeNowMS();
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return timeNowMS() - now;
}

u32 strhash(const char* s)
{
    u32 hash = 0;
    while (*s)
        hash = hash * 101 + *s++;
    return hash;
}

u32 roundPow2(u32 v)
{
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    v++;
    return v;
}

char* sizetostr_unsafe(char* buf, size_t bufsz, size_t num)
{
    char *p = buf + bufsz - 1;
    *p-- = 0;
    if(!num)
        *p-- = '0';
    else do
    {
        size_t div = num / 10;
        size_t rem = num - (div * 10);
        assert(rem < 10);
        num = div;
        *p-- = '0' + rem;
    }
    while(num);
    return p+1;
}
