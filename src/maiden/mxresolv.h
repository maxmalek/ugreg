#pragma once

#include <string>
#include <vector>
#include "types.h"

struct MxResolvResult
{
    void parse(const char *s); // "host:port"
    bool operator<(const MxResolvResult& o) const;
    bool validate();

    std::string host;
    unsigned port;

    // only for sorting; can be ignored otherwise
    int priority;
    unsigned weight;
};

typedef std::vector<MxResolvResult> MxResolvList;

// 0 = generic failure, otherwise http status (200 is ok)
MxResolvList lookupHomeserverForHost(const char *host, u64 timeoutMS, size_t maxsize);
