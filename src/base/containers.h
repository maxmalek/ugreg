#pragma once

#include <assert.h>
#include "mem.h"

// Why not STL containers?
// Mainly because these have a function to swap out the underlying storage array
// so that it can be moved elsewhere easily without having to copy everything


template<typename T>
struct DefaultPolicy
{
    typedef BlockAllocator Allocator;
    inline static void OnDestroy(Allocator&, T&) {}
};

#if 1
// Light vector -- needs external allocator
template<
    typename T,
    typename SZ = size_t,
    typename Policy = DefaultPolicy<T>
>
class LVector
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
    LVector(Allocator& mem, SZ sz)
        : _ptr((T*)mem.Alloc(sz * sizeof(T)))
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
        size_t sz = size();
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
#endif

#if 0
template<typename T>
class XVectorBase
{
public:
    template<typename SZ>
    struct InfoFromSizeType
    {
        SZ sz, cap;
    };
protected:
    T *_ptr;
};

// Extra-light vector -- just a wrapper around a pointer, needs external size and capacity
template<
    typename T,
    typename Info,
    typename Policy = DefaultPolicy<T>
>
class XVector : public XVectorBase<T>
{
    typedef typename Policy::Allocator Allocator;
public:

    XVector() : _ptr(NULL) {}
    ~XVector()
    {
        assert(!_ptr && "LVector not dealloc'd!");
    }
    XVector(Allocator& mem, Info q)
        : _ptr((T*)mem.Alloc(sz * sizeof(T)))
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
        if (!_ptr)
            return;
        clear(mem);
        mem.Free(_ptr, _cap * sizeof(T));
        _ptr = NULL;
        _cap = NULL;
    }

    // given current size info, adjust size & cap to new size info
    T* adjust(Allocator& mem, const Info current, const Info desired)
    {
        T *dst;
        if (current.cap < desired.cap)
        {
            dst = (T*)mem.Alloc(desired.cap * sizeof(T));
            mem_construct_move_from(dst, dst + current.sz, _ptr); // move old elems over
            _Destroy(mem, _ptr, _ptr + current.sz);               // kill moved-from elems for good
            mem.Free(_ptr, _cap * sizeof(T));                     // free it
            _ptr = dst;
        }
        else
            dst = _ptr;

        if(current.sz < desired.sz)
            mem_construct_default(dst + current.sz, dst + desired.sz); // construct new tail
        else
            _Destroy(mem, dst + current.sz, dst + desired.sz);    // destroy some at end

        return dst;
    }

    // remove storage from vector
    T* detach()
    {
        T* p = _ptr;
        _ptr = NULL;
        return p;
    }

    T& push_back(Allocator& mem, const Info q, T&& x)
    {
        Info desired(q);
        ++desired.sz;
        T* p = adjust(mem, q, desired);
        return *(_X_PLACEMENT_NEW(p + sz) T(std::move(x)));
    }

private:
    static void _Destroy(Allocator& mem, T* const begin, T* const end)
    {
        for (T* p = begin; p < end; ++p)
            Policy::OnDestroy(mem, *p);
        mem_destruct(begin, end);
    }
};

template<
    typename T,
    typename SZ = size_t,
    typename Policy = DefaultPolicy<T>
>
class LVector : private XVector<T, typename XVectorBase::template InfoFromSizeType<SZ>, Policy>
{
    typedef typename Policy::Allocator Allocator;
public:
    LVector() : _ptr(NULL), _sz(0), _cap(0) {}
    ~LVector()
    {
        assert(!_ptr && "LVector not dealloc'd!");
    }
    LVector(Allocator& mem, SZ sz)
        : _ptr((T*)mem.Alloc(sz * sizeof(T)))
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
        if (!_ptr)
            return;
        clear(mem);
        mem.Free(_ptr, _cap * sizeof(T));
        _ptr = NULL;
        _cap = NULL;
    }
    void resize(Allocator& mem, SZ n)
    {
        if (n > _sz)
        {
            T* dst = reserve(n)                         // make sure there's enough space
                mem_construct_default(dst + _sz, dst + n);  // fill up with valid objects
        }
        else
            _Destroy(mem, _ptr + n, _ptr + _sz);             // destroy some at end
        _sz = n;
    }
    T* reserve(Allocator& mem, SZ n)
    {
        if (n <= _cap)
            return _ptr;

        T* dst = (T*)mem.Alloc(n * sizeof(T));
        mem_construct_move_from(dst, dst + _sz, _ptr); // move old elems over
        _Destroy(mem, _ptr, _ptr + _sz);                    // kill moved-from elems for good
        mem.Free(_ptr, _cap * sizeof(T));              // free it
        _ptr = dst;
        _cap = n;
        return dst;
    }

    inline SZ size() const { return _sz; }

    // remove storage from vector
    T* detach()
    {
        T* p = _ptr;
        _ptr = NULL;
        _sz = 0;
        _cap = 0;
    }

    T& push_back(Allocator& mem, T&& x)
    {
        size_t sz = size();
        _sz = sz + 1;
        T* p = reserve(mem, sz + 1);
        return *(_X_PLACEMENT_NEW(p + sz) T(std::move(x)));
    }

private:
    static void _Destroy(Allocator& mem, T* const begin, T* const end)
    {
        for (T* p = begin; p < end; ++p)
            Policy::OnDestroy(mem, *p);
        mem_destruct(begin, end);
    }
    T* _ptr;
    SZ _sz;
    SZ _cap;
};
#endif

#if 0

// Basic std::vector replacement
template<typename T>
class Vec : 
{
private:
    T *_data;
    size_t _sz, _cap;
    BasicAllocator _alloc;

public:
    Vec()
        : _data(0), _sz(0), _cap(0) {}
    Vec(size_t n)
        : _data(_alloc(n)), _sz(n), _cap(n) {}
    ~Vec()
    {
        dealloc();
    }

    void dealloc()
    {
        clear();
        _alloc(
        _cap = 0;
    }

    void clear()
    {
        mem_deconstruct(_data, _data + _sz);
        _sz = 0;
    }

    void reserve(size_t n);
    void resize(size_t n);
    inline size_t size() const { return _sz; }

    // maybe NULL
    inline       T *ptr()       { return _p; }
    inline const T *ptr() const { return _p; }

    inline       T& operator[](size_t i)       { _bound(i); return _data[i]; }
    inline const T& operator[](size_t i) const { _bound(i); return _data[i]; }

    T *

    void push_back(const T& x) { _



private:
    void _bound(size_t i) { assert(i < _sz); }
    static _Invalidate(size_t beg, size_t end);
};

#endif
