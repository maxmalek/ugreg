#pragma once

#include <vector>
#include "strmatch.h"

typedef std::vector<TwoWayCasefoldMatcher> MxMatcherList;

MxMatcherList mxBuildMatchersForTerm(const char *term);
bool mxSearchNormalizeAppend(std::vector<unsigned char>& vec, const char *s, size_t len);

int mxMatchAndScore_Exact(const char *haystack, size_t haylen, const TwoWayCasefoldMatcher *matchers, size_t nummatchers);
int mxMatchAndScore_Fuzzy(const char *haystack, const TwoWayCasefoldMatcher *matchers, size_t nummatchers);
