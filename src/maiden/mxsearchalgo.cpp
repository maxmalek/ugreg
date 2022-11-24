#include "mxsearchalgo.h"
#include "variant.h"
#include "strmatch.h"

// TODO: this should be using some variation of two-way string matching
// see https://git.musl-libc.org/cgit/musl/tree/src/string/memmem.c

static int scorePartialMatch(const char *full, const char *part)
{

}

void mxSearchNormalizeAppend(std::vector<char>& vec, const char* s, size_t len)
{
    // TODO: utf-8 normalize, lowercase, etc

    if(vec.size())
        vec.push_back(0);

    for(size_t i = 0; i < len; ++i)
        vec.push_back(s[i]);

}
