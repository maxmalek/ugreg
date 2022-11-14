#pragma once

#include <assert.h>
#include "mem.h"

// Why not STL containers?
// Mainly because these have a function to swap out the underlying storage array
// so that it can be moved elsewhere easily without having to copy everything


template<typename T>
struct ContainerDefaultPolicy
{
    typedef BlockAllocator Allocator;
    inline static void OnDestroy(Allocator&, T&) {}
};

struct LVectorBase
{
    struct ReserveTag {}; // just reserve space but don't init
    struct InitTag {}; // resize+init behavior
};

// Light vector -- needs external allocator
template<
    typename T,
    typename SZ = size_t,
    typename Policy = ContainerDefaultPolicy<T>
>
class LVector : public LVectorBase
{
    typedef typename Policy::Allocator Allocator;
public:
    typedef T value_type;
    typedef SZ size_type;

    // this is the most lazy thing and should suffice
    typedef T* iterator;
    typedef const T* const_iterator;

    LVector() : _ptr(NULL), _sz(0), _cap(0) {}
    ~LVector()
    {
        assert(!_ptr && "LVector not dealloc'd!");
    }
    LVector(Allocator& mem, SZ sz, ReserveTag)
        : _ptr((T*)(sz ? mem.Alloc(sz * sizeof(T)) : NULL))
        , _sz(0), _cap(sz)
    {
    }

    LVector(Allocator& mem, SZ sz, InitTag)
        : _ptr((T*)(sz ? mem.Alloc(sz * sizeof(T)) : NULL))
        , _sz(sz), _cap(sz)
    {
        mem_construct_default(_ptr, _ptr + sz);
    }

    LVector(LVector&& o) noexcept
        : _sz(o._sz), _cap(o._cap)
    {
        _ptr = o.detach();
    }

    LVector& operator=(LVector&& o) noexcept = delete;
    LVector(const LVector&) = delete;
    LVector& operator=(const LVector&) = delete;

    inline T& operator[](SZ idx)
    {
        assert(idx < _sz);
        return _ptr[idx];
    }
    inline const T& operator[](SZ idx) const
    {
        assert(idx < _sz);
        return _ptr[idx];
    }

    void clear(Allocator& mem)
    {
        const SZ n = _sz;
        _sz = 0;
        _Destroy(mem, _ptr, _ptr + n);
    }
    void dealloc(Allocator& mem)
    {
        if(!_ptr)
            return;
        clear(mem);
        mem.Free(_ptr, _cap * sizeof(T));
        _ptr = NULL;
        _cap = 0;
    }
    void resize(Allocator& mem, SZ n)
    {
        if(n > _sz)
        {
            T *dst = reserve(mem, n);                  // make sure there's enough space
            mem_construct_default(dst + _sz, dst + n); // fill up with valid objects
        }
        else
            _Destroy(mem, _ptr + n, _ptr + _sz);       // destroy some at end
        _sz = n;
    }
    T *_reserveAtLeast(Allocator& mem, SZ n)
    {
        if (n <= _cap)
            return _ptr;
        return _enlarge(mem, (_cap + (_cap / 2) + 4 + 3) & ~3); // I'm sorry
    }
    T *reserve(Allocator& mem, SZ n)
    {
        if (n <= _cap)
            return _ptr;
        return _enlarge(mem, n);
    }
    T *_enlarge(Allocator& mem, SZ n)
    {
        assert(n > _cap && _sz <= _cap);
        T *dst = (T*)mem.Alloc(n * sizeof(T));
        mem_construct_move_from(dst, dst + _sz, _ptr); // move old elems over
        _Destroy(mem, _ptr, _ptr + _sz);               // kill moved-from elems for good
        mem.Free(_ptr, _cap * sizeof(T));              // free it
        _ptr = dst;
        _cap = n;
        return dst;
    }

    inline bool empty() const { return !_sz; }
    inline SZ size() const { return _sz; }
    inline T *data() { return _ptr; }
    inline const T *data() const { return _ptr; }
    inline iterator begin() { return _ptr; }
    inline iterator end()   { return _ptr + _sz; }
    inline const_iterator begin() const { return _ptr; }
    inline const_iterator end()   const { return _ptr + _sz; }

    // remove storage from vector
    T *detach()
    {
        T *p = _ptr;
        _ptr = NULL;
        _sz = 0;
        _cap = 0;
        return p;
    }

    T& push_back(Allocator& mem, T&& x)
    {
        SZ sz = size();
        T *p = _reserveAtLeast(mem, sz + 1);
        _sz = sz + 1;
        return *(_X_PLACEMENT_NEW(p + sz) T(std::move(x)));
    }

    void swap(LVector& o)
    {
        std::swap(_ptr, o._ptr);
        std::swap(_sz, o._sz);
        std::swap(_cap, o._cap);
    }

    T& back()
    {
        assert(_sz);
        return _ptr[_sz - 1];
    }

    const T& back() const
    {
        assert(_sz);
        return _ptr[_sz - 1];
    }

    void pop_back(Allocator& mem)
    {
        assert(_sz);
        const size_t sz = _sz - 1;
        _sz = sz;
        T& bk = _ptr[sz];
        Policy::OnDestroy(mem, bk);
        bk.~T();
    }

    T pop_back_move()
    {
        assert(_sz);
        const size_t sz = _sz - 1;
        _sz = sz;
        T bk = std::move(_ptr[sz]);
        return bk; // copy elision / named return value always moves
    }

private:
    static void _Destroy(Allocator& mem, T * const begin, T * const end)
    {
        for (T *p = begin; p < end; ++p)
            Policy::OnDestroy(mem, *p);
        mem_destruct(begin, end);
    }
    T* _ptr;
    SZ _sz;
    SZ _cap;
};
