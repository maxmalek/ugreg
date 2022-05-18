#include "mxtoken.h"
#include "rng.h"
#include <random>
#include <limits>
#include <assert.h>
#include "util.h"

// adapter for std::uniform_int_distribution
struct RngEngine
{
    typedef u64 result_type;
    MixRand& _r;
    RngEngine(MixRand& r) : _r(r) {}
    result_type operator()() { return _r.next(); }

    static constexpr result_type min() { return std::numeric_limits<result_type>::min(); }
    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }
};

void mxGenerateToken(char * dst, size_t n, const char * alphabet, size_t alphabetSize)
{
    assert(alphabetSize);
    assert(n);
    if(!n || !alphabetSize)
        return;

    std::uniform_int_distribution dist(0, int(alphabetSize - 1));
    RngEngine eng(GetThreadRng());

    --n;
    for(size_t i = 0; i < n; ++i)
    {
        int x = dist(eng);
        dst[i] = alphabet[x];
    }
    dst[n] = 0;
}


static const char s_alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_01234567890";

void mxGenerateToken(char* dst, size_t n)
{
    mxGenerateToken(dst, n, s_alphabet, sizeof(s_alphabet) - 1);
}
