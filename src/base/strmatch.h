#pragma once

// Two-way string matching
// https://en.wikipedia.org/wiki/Two-way_string-matching_algorithm
// This class is intended for precalculating the search for one needle
// and then perform many searches, amortizing the precalc costs.

#include "types.h"
#include <vector>

class TwoWayMatcher
{
public:
    TwoWayMatcher(const char *needle, size_t len);

    const char *match(const char *haystack, size_t len) const;

protected:
    TwoWayMatcher();
    void init();
    std::vector<unsigned char> _needle;
private:
    TwoWayMatcher& operator=(const TwoWayMatcher&) = delete;
    TwoWayMatcher(const TwoWayMatcher&) = delete;

    size_t mem0, p, ms;
    size_t byteset[32 / sizeof(size_t)];
    size_t shift[256];

    const unsigned char* twoway_match(const unsigned char *h, const unsigned char *z) const;

};

class TwoWayCasefoldMatcher : public TwoWayMatcher
{
public:
    TwoWayCasefoldMatcher(const char *needle, size_t len);
};
