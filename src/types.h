#pragma once

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

typedef int32_t s32;
typedef uint32_t u32;

typedef int64_t s64;
typedef uint64_t u64;

// This is important! strpool.h uses STRPOOL_U64 by default, which is why this needs to be 64 bits
typedef u64 StrRef;

struct PoolStr
{
    const char* s;
    size_t len;
};
