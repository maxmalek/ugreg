#include "mxtoken.h"
#include "rng.h"
#include <random>
#include <limits>
#include <assert.h>
#include "util.h"



void mxGenerateToken(char * dst, size_t n, const char * alphabet, size_t alphabetSize)
{
    assert(alphabetSize);
    assert(n);
    if(!n || !alphabetSize)
        return;

    std::uniform_int_distribution dist(0, int(alphabetSize - 1));
    RngEngine eng;

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
