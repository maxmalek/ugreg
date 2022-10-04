#pragma once

#include <string>
#include <vector>
#include "types.h"
#include "webstuff.h"

struct MxResolvResult
{
    bool parse(const char *s);
    bool operator<(const MxResolvResult& o) const;
    bool validate();

    URLTarget target;

    // only for sorting; can be ignored otherwise
    int priority;
    unsigned weight;
};

typedef std::vector<MxResolvResult> MxResolvList;

// 0 = generic failure, otherwise http status (200 is ok)
MxResolvList lookupHomeserverForHost(const char *host, u64 timeoutMS, size_t maxsize);
