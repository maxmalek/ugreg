#include "treemem.h"
#include "luaalloc.h"
#include "strpool.h"
#include <limits>
#include <assert.h>


TreeMem::TreeMem()
    : _LA(luaalloc_create(NULL, NULL))
{
    strpool_init(&_sp, &strpool_default_config);
}

TreeMem::~TreeMem()
{
    strpool_term(&_sp);
    luaalloc_delete(_LA);
}

// HACK // FIXME
// Since the dumb stringpool thing uses 0 for "" and for NULL, we use u64(-1) for "".
// Ideally we'd replace the string pool impl with something sensible,
// but this should do for now considering that the generation count of u64(-1) is so high
// that in practice it's never reached. So that value *should* be safe. For now.

static constexpr u64 EMPTY_REF = u64(-1);

const char* TreeMem::getS(StrRef s) const
{
    return s != EMPTY_REF ? strpool_cstr(&_sp, s) : "";
}

PoolStr TreeMem::getSL(StrRef s) const
{
    PoolStr ps;
    if(s != EMPTY_REF)
    {
        ps.s = getS(s);
        ps.len = (size_t)strpool_length(&_sp, s);
    }
    else
    {
        ps.s = "";
        ps.len = 0;
    }
    return ps;
}

size_t TreeMem::getL(StrRef s) const
{
    return (size_t)strpool_length(&_sp, s);
}

StrRef TreeMem::lookup(const char* s, size_t len) const
{
    assert(s);
    return *s ? strpool_lookup(&_sp, s, len) : EMPTY_REF;
}

StrRef TreeMem::putNoRefcount(const char* s, size_t len)
{
    if (!*s)
        return EMPTY_REF;
    return strpool_inject(&_sp, s, len);
}

void TreeMem::increfS(StrRef s)
{
    strpool_incref(&_sp, s);
}

StrRef TreeMem::put(const char* s, size_t len)
{
    assert(len < (std::numeric_limits<int>::max()));
    if(!*s)
        return EMPTY_REF;
    StrRef ref = strpool_inject(&_sp, s, len);
    strpool_incref(&_sp, ref);
    return ref;
}

const char* TreeMem::putS(const char* s, size_t len)
{
    assert(s);
    return *s ? strpool_cstr(&_sp, this->put(s, len)) : s;
}

void TreeMem::freeS(StrRef s)
{
    if(s == EMPTY_REF)
        return;
    if(!strpool_decref(&_sp, s))
        strpool_discard(&_sp, s);
}

char* TreeMem::collate(size_t *n) const // TESTING ONLY, LEAKS LIKE FUCK
{
    int i = 0;
    char *c = strpool_collate(&_sp, &i);
    *n = i;
    return c;
}

void* TreeMem::Alloc(size_t sz)
{
    return luaalloc(_LA, NULL, 0, sz);
}

void TreeMem::Free(void* p, size_t sz)
{
    luaalloc(_LA, p, sz, 0);
}
