#include "mxsearchalgo.h"
#include "utf8casefold.h"
#include <assert.h>

// TODO: this should be using some variation of two-way string matching
// see https://git.musl-libc.org/cgit/musl/tree/src/string/memmem.c

static int scorePartialMatch(const char *full, const char *part)
{

}

bool mxSearchNormalizeAppend(std::vector<unsigned char>& vec, const char* s, size_t len)
{
    assert(len);

    if(vec.size()) // terminate previous thing, if any
        vec.push_back(0);

    return utf8casefoldcopy(vec, s, len) > 0;
}
