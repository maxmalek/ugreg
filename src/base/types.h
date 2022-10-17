#pragma once

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 26812) // The enum type ... is unscoped. Prefer 'enum class' over 'enum' (Enum.3).
#endif

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>

typedef int8_t s8;
typedef uint8_t u8;

typedef int16_t s16;
typedef uint16_t u16;

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
