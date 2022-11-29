#include "mxsearchalgo.h"
#include "utf8casefold.h"
#include "strmatch.h"
#include <assert.h>
#include <ctype.h>
#include "fts_fuzzy_match.h"

// TODO: this should be using some variation of two-way string matching
// see https://git.musl-libc.org/cgit/musl/tree/src/string/memmem.c

// How to score exact matches?
// If we search for a term and a word starts with it, it's obviously a better match than if it's somewhere in the middle.


static bool splitsWords(unsigned char c)
{
    return c < 128 && (!c || isspace(c) || ispunct(c) || iscntrl(c));
}

static bool isWordStart(const char *haystack, size_t haylen, const char *match)
{
    if(haystack == match)
        return true;

    /*const char *beg = utf8FindBeginBackward(match - 1); // = (match - 1) but in a utf8-correct way
    if(!beg)
        return false;
    size_t remain = haylen - (haystack - match);
    int c = utf8read(match, remain);*/

    // why utf8-correct? anything we check for is ASCII so don't need to decode utf8 for now
    return splitsWords(match[-1]);
}

MxMatcherList mxBuildMatchersForTerm(const char *term)
{
    MxMatcherList ret;
    const char *begin = term;
    const char *p = begin;
    for(;;)
    {
        if(splitsWords(*p))
        {
            if(p > begin) // don't want an empty matcher (that matches everything). need at least 1 char.
                ret.emplace_back(begin, p - begin);
            begin = p + 1;
        }
        if(!*p)
            break;
        ++p;
    }
    return ret;
}


int mxMatchAndScore_Exact(const char *haystack, size_t haylen, const TwoWayCasefoldMatcher *matchers, size_t nummatchers)
{
    int score = 0;
    for(size_t i = 0; i < nummatchers; ++i)
    {
        int bestmatch = 0;
        const char *begin = haystack;
        for(;;)
        {
            // like strstr() but faster and doesn't stop on \0
            const char *match = matchers[i].match(begin, haylen);
            if(!match)
                break;
            if(isWordStart(haystack, haylen, match)) // match begins a word?
            {
                bestmatch = 100000;

                // after the match is a word boundary, or end of string?
                const char *wend = match + matchers[i].needleSize();
                if(wend >= haystack + haylen || splitsWords(*wend))
                {
                    bestmatch += 100000;
                    break; // exact word match, can't get better than this
                }
            }
            bestmatch = std::max(bestmatch, 10000);
            ++begin;
        }
        // all terms must match somehow. if one doesn't match, get out.
        if(!bestmatch)
            return 0;
        score += bestmatch;
    }
    return score;
}

int mxMatchAndScore_Fuzzy(const char* haystack, const TwoWayCasefoldMatcher* matchers, size_t nummatchers)
{
    int score = 0;
    for (size_t i = 0; i < nummatchers; ++i)
    {
        int bestmatch = 0;
        if(fts::fuzzy_match(matchers[i].needle(), haystack, bestmatch))
            score += bestmatch;
    }
    return score;
}

bool mxSearchNormalizeAppend(std::vector<unsigned char>& vec, const char* s, size_t len)
{
    assert(len);

    if(vec.size()) // terminate previous thing, if any
        vec.push_back(0);

    return utf8casefoldcopy(vec, s, len) > 0;
}
