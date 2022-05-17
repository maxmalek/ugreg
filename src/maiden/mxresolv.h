#pragma once

#include <string>
#include "types.h"

// 0 = generic failure, otherwise http status (200 is ok)
int lookupHomeserverForHost(std::string& dst, const char *host, u64 timeoutMS, size_t maxsize);
