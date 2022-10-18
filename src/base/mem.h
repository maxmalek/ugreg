#pragma once

#include <stddef.h>
#include <utility> // std::move
#include "types.h"
#include "strpool.h"
#include <assert.h>
#include <vector>
#include <string>

// operator new() without #include <new>
// Unfortunately the standard mandates the use of size_t, so we need stddef.h the very least.
// Trick via https://github.com/ocornut/imgui
// "Defining a custom placement new() with a dummy parameter allows us to bypass including <new>
// which on some platforms complains when user has disabled exceptions."
struct _X__NewDummy {};
inline void* operator new(size_t, _X__NewDummy, void* ptr) { return ptr; }
inline void  operator delete(void*, _X__NewDummy, void*) {}
#define _X_PLACEMENT_NEW(p) new(_X__NewDummy(), p)

// Helpers for memory range manipulation
template<typename T>
T *mem_destruct(T* const begin, T* const end)
{
    for (T* p = begin; p < end; ++p)
        p->~T();
    return begin;
}

template<typename T>
T *mem_construct_default(T* const begin, T* const end)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T();
    return begin;
}

template<typename T>
T* mem_construct_from(T* const begin, T* const end, const T& x)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T(x);
    return begin;
}

// not needed for now
template<typename T>
T *mem_construct_move_from(T* const begin, T* const end, T *movebegin)
{
    for (T* p = begin; p < end; ++p)
        _X_PLACEMENT_NEW(p) T(std::move(*movebegin++));
    return begin;
}


struct LuaAlloc;

class BlockAllocator
{
public:
    BlockAllocator();
    ~BlockAllocator();
    // --- block allocator -- (capitalized not to conflict with some debug #defines or whatever)
    void* Alloc(size_t sz);
    void* Realloc(void* p, size_t oldsize, size_t newsize);
    void Free(void* p, size_t sz);

    LuaAlloc *getLuaAllocPtr() { return _LA; }
    const LuaAlloc * getLuaAllocPtr() const { return _LA; }

private:
    LuaAlloc* _LA; // It's intended for Lua but it's ok as a stand-alone block allocator
};

class StringPool
{
public:
    enum PoolSize
    {
        DEFAULT,
        TINY,
        SMALL,
    };
    StringPool(PoolSize s);
    ~StringPool();

    // lookup string by ref. does not touch the refcount
    const char* getS(StrRef s) const;
    PoolStr getSL(StrRef s) const;
    size_t getL(StrRef s) const;

    // lookup ref by string
    StrRef lookup(const char* s, size_t len) const;

    // store in string pool and increase refcount; if already stored; just increase the refcount
    StrRef put(const char* s, size_t len);
    const char* putS(const char* s, size_t len); // like put(), but returns pointer to internalized mem
    inline PoolStr putSL(const char* s, size_t len)
    {
        PoolStr ps{ putS(s, len), len };
        return ps;
    }

    StrRef putNoRefcount(const char* s, size_t len);
    void increfS(StrRef s);

    // decrease refcount and drop if no longer referenced
    void freeS(StrRef s);

    struct StrAndCount
    {
        std::string s;
        size_t count = 0;
        StrRef ref = 0;
    };
    typedef std::vector<StrAndCount> StrColl;

    StrColl collate() const; // quite expensive to call
    void defrag();

    // translate one foreign StrRef to own memory space. Does not add or incref in own pool.
    StrRef translateS(const StringPool& other, StrRef s) const;

private:
    strpool_t _sp;
};
