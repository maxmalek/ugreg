#include "util.h"
#include <chrono>
#include <assert.h>
#include <thread>
#include "safe_numerics.h"

template<typename T>
static NumConvertResult strtounum_T_NN(T* dst, const char* s, size_t len)
{
    NumConvertResult res { 0, false };
    T k = 0;
    if(len) do
    {
        unsigned char c = s[res.used++]; // increment here...
        if(c >= '0' && c <= '9')
        {
            res.overflow |= mul_check_overflow<T>(&k, k, 10);
            res.overflow |= add_check_overflow<T>(&k, k, c - '0');
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

NumConvertResult strtosizeNN(size_t* dst, const char* s, size_t len)
{
    return strtounum_T_NN<size_t>(dst, s, len);
}

NumConvertResult strtou64NN(u64* dst, const char* s, size_t len)
{
    return strtounum_T_NN<u64>(dst, s, len);
}

NumConvertResult strtoi64NN(s64* dst, const char* s, size_t len)
{
    u64 tmp;
    const bool neg = len && *s == '-';
    s += neg;
    len -= neg;
    NumConvertResult res = strtounum_T_NN<u64>(&tmp, s, len);
    if(res.ok())
    {
        if(isValidNumericCast<s64>(tmp))
        {
            res.used += neg;
            *dst = neg ? -s64(tmp) : s64(tmp);
        }
        else
            res.overflow = true;
    }
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
                default: s = beg; [[fallthrough]];
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
        *p-- = '0' + (char)rem;
    }
    while(num);
    return p+1;
}

size_t base64size(size_t len)
{
    return len * 8 / 6 + 4;
}

// base64 conversion from civetweb and changed int->size_t and param order
// unfortunately it's a static func inside civetweb so had to copy it out
size_t base64enc(char* dst, const unsigned char* src, size_t src_len)
{
    static const char* b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, j;
    unsigned a, b, c;

    for (i = j = 0; i < src_len; i += 3) {
        a = src[i];
        b = ((i + 1) >= src_len) ? 0 : src[i + 1];
        c = ((i + 2) >= src_len) ? 0 : src[i + 2];

        dst[j++] = b64[a >> 2];
        dst[j++] = b64[((a & 3) << 4) | (b >> 4)];
        if (i + 1 < src_len) {
            dst[j++] = b64[(b & 15) << 2 | (c >> 6)];
        }
        if (i + 2 < src_len) {
            dst[j++] = b64[c & 63];
        }
    }
    while (j % 4 != 0) {
        dst[j++] = '=';
    }
    dst[j] = '\0';
    return j;
}

static unsigned char
b64reverse(char letter)
{
    if ((letter >= 'A') && (letter <= 'Z')) {
        return letter - 'A';
    }
    if ((letter >= 'a') && (letter <= 'z')) {
        return letter - 'a' + 26;
    }
    if ((letter >= '0') && (letter <= '9')) {
        return letter - '0' + 52;
    }
    if (letter == '+') {
        return 62;
    }
    if (letter == '/') {
        return 63;
    }
    if (letter == '=') {
        return 255; /* normal end */
    }
    return 254; /* error */
}

size_t base64dec(char* dst, size_t* dst_len, const unsigned char* src, size_t src_len)
{
    *dst_len = 0;

    for (size_t i = 0; i < src_len; i += 4) {
        unsigned char a = b64reverse(src[i]);
        if (a >= 254) {
            return i;
        }

        unsigned char b = b64reverse(((i + 1) >= src_len) ? 0 : src[i + 1]);
        if (b >= 254) {
            return i + 1;
        }

        unsigned char c = b64reverse(((i + 2) >= src_len) ? 0 : src[i + 2]);
        if (c == 254) {
            return i + 2;
        }

        unsigned char d = b64reverse(((i + 3) >= src_len) ? 0 : src[i + 3]);
        if (d == 254) {
            return i + 3;
        }

        dst[(*dst_len)++] = (a << 2) + (b >> 4);
        if (c != 255) {
            dst[(*dst_len)++] = (b << 4) + (c >> 2);
            if (d != 255) {
                dst[(*dst_len)++] = (c << 6) + d;
            }
        }
    }
    return 0;
}

