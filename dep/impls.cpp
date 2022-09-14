// All implementations for single-file libs go here

#include "luaalloc.h"

// Since strpool.h does its own memory management this is not necessary and has no benefit.

/*
static void *strpool_LA_malloc(void *ctx, size_t sz)
{
    size_t *p = (size_t*)luaalloc(ctx, 0, 0, sz + sizeof(size_t));
    *p++ = sz;
    return p;
}

static void strpool_LA_free(void* ctx, void *ptr)
{
    size_t *p = (size_t*)ptr;
    size_t sz = *--p;
    luaalloc(ctx, p, sz + sizeof(size_t), 0);
}

#define STRPOOL_MALLOC( ctx, size ) ( strpool_LA_malloc( ctx, size ) )
#define STRPOOL_FREE( ctx, ptr ) ( strpool_LA_free( ctx, ptr ) )
*/

#define STRPOOL_IMPLEMENTATION
#include "strpool.h"

#define MINICORO_IMPL
#include "minicoro.h"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"
