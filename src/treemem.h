/* Backing memory for a tree of Var things.
Has optimizations for:
- Strings are deduplicated and stored in a string pool
- Small memory blocks are allocated using a block allocator

Warning: NOT thread safe!
Unlike malloc/free, this class' Free() needs the size, and it must be the original size as
passed to the allocation.
*/

#pragma once

#include "types.h"
#include "strpool.h"

struct LuaAlloc;

class TreeMem
{
public:
    TreeMem();
    ~TreeMem();

    // --- string pool ---

    // lookup string by ref. does not touch the refcount
    const char *getS(StrRef s) const;
    PoolStr getSL(StrRef s) const;
    size_t getL(StrRef s) const;

    // lookup ref by string
    StrRef lookup(const char *s, size_t len) const;

    // store in string pool and increase refcount; if already stored; just increase the refcount
    StrRef put(const char *s, size_t len);
    const char *putS(const char *s, size_t len); // like put(), but returns pointer to internalized mem
    inline PoolStr putSL(const char* s, size_t len)
    {
        PoolStr ps { putS(s, len), len };
        return ps;
    }

    StrRef putNoRefcount(const char* s, size_t len);
    void increfS(StrRef s);

    // decrease refcount and drop if no longer referenced
    void freeS(StrRef s);

    char *collate(size_t *n) const;

    // --- block allocator -- (lowercase not to conflict with some debug #defines or whatever)
    void *Alloc(size_t sz);
    void *Realloc(void* p, size_t oldsize, size_t newsize);
    void Free(void *p, size_t sz);

private:
    LuaAlloc * const _LA; // It's intended for Lua but it's ok as a stand-alone block allocator
    strpool_t _sp;
};
