#pragma once

#include <string>
#include "types.h"

struct MxResolvResult
{
    std::string host;
    unsigned port;
};

// 0 = generic failure, otherwise http status (200 is ok)
MxResolvResult lookupHomeserverForHost(const char *host, u64 timeoutMS, size_t maxsize);
