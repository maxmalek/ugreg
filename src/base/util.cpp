#include "util.h"
#include <chrono>
#include <assert.h>
#include <thread>
#include "safe_numerics.h"
#include "tomcrypt/tomcrypt.h"

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
    return (4 * ((len + 2) / 3)) + 1;
}

size_t base64enc(char* dst, size_t dstlen, const unsigned char* src, size_t src_len, bool pad)
{
    unsigned long dstlen_ = dstlen;
    int r = pad ? base64_encode         (src, src_len, (unsigned char*)dst, &dstlen_)
                : base64_encode_unpadded(src, src_len, (unsigned char*)dst, &dstlen_);
    assert(r != CRYPT_BUFFER_OVERFLOW);
    return r == CRYPT_OK ? dstlen_ : 0;
}

size_t base64dec(char* dst, size_t dstlen, const unsigned char* src, size_t src_len, bool strict)
{
    unsigned long dstlen_ = dstlen;
    int r = strict ? base64_strict_decode(src, src_len, (unsigned char*)dst, &dstlen_)
                   : base64_decode       (src, src_len, (unsigned char*)dst, &dstlen_);
    assert(r != CRYPT_BUFFER_OVERFLOW);
    return r == CRYPT_OK ? dstlen_ : 0;
}

size_t hash_oneshot(char* dst, const void* src, size_t len, const ltc_hash_descriptor *hd)
{
    if(dst)
    {
        hash_state h;
        hd->init(&h);
        hd->process(&h, (const unsigned char*)src, (unsigned long)len);
        hd->done(&h, (unsigned char*)dst);
    }
    return hd->hashsize;
}

size_t hash_sha256(char* dst, const void* src, size_t len)
{
    return hash_oneshot(dst, src, len, &sha256_desc);
}

size_t hash_sha512(char* dst, const void* src, size_t len)
{
    return hash_oneshot(dst, src, len, &sha512_desc);
}

size_t hash_sha3_512(char* dst, const void* src, size_t len)
{
    return hash_oneshot(dst, src, len, &sha3_512_desc);
}

static const ltc_hash_descriptor * const s_hashdesc[] =
{
    &sha256_desc,
    &sha512_desc,
    &sha3_224_desc,
    &sha3_256_desc,
    &sha3_384_desc,
    &sha3_512_desc,
    NULL
};

const ltc_hash_descriptor* const* hash_alldesc()
{
    return s_hashdesc;
}

const ltc_hash_descriptor* hash_getdesc(const char* name)
{
    for(size_t i = 0; s_hashdesc[i]; ++i)
        if(!strcmp(s_hashdesc[i]->name, name))
            return s_hashdesc[i];
    return NULL;
}

void hash_testall()
{
    bool fail = false;
    DEBUG_LOG("-- tomcrypt hash test --");
    for(size_t i = 0; s_hashdesc[i]; ++i)
    {
        const char *name = s_hashdesc[i]->name;
        switch(s_hashdesc[i]->test())
        {
            case CRYPT_OK:  DEBUG_LOG("%-12s ... ok", name); break;
            case CRYPT_NOP: DEBUG_LOG("%-12s ... not tested"); break;
            default:        logerror ("%-12s ... FAILED"); fail = true; break;
        }
    }
    if(fail)
    {
        logerror("Failed hash self-test. Something mis-compiled?");
        exit(23);
    }
    DEBUG_LOG("-- hash test done, seems ok --");
}
