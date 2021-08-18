#pragma once

#include <atomic>

class Refcounted
{
public:
    typedef bool is_refcounted; // tag for some compile-time checks
protected:
    inline Refcounted() : _refcount(0) {}
    // refcount starts at 0, even if copy constructed
    inline Refcounted(const Refcounted&) : _refcount(0) {}
    virtual ~Refcounted() {}
    mutable std::atomic_uint _refcount;
public:
    inline void incref() const { ++_refcount; }
    inline void decref() const { if(!--_refcount) delete this; }
};

template<typename T>
class CountedPtr
{
public:
    typedef T value_type;

    inline ~CountedPtr()
    {
        if(_p)
            _p->decref();
    }
    inline CountedPtr() : _p(0)
    {}
    inline CountedPtr(T* p) : _p(p)
    {
        if(p)
            _p->incref();
    }
    inline CountedPtr(const CountedPtr& ref) : _p(ref._p)
    {
        if (_p)
            _p->incref();
    }

    CountedPtr& operator=(const CountedPtr& ref)
    {
        if(ref._p)
            ref._p.incref();
        _p = ref._p;
        return *this;
    }
    CountedPtr(CountedPtr&& ref) : _p(ref._p)
    {
        ref._p = 0;
    }
    CountedPtr& operator=(CountedPtr&& ref) noexcept
    {
        T* const oldp = _p;
        _p = ref._p;
        ref._p = 0;
        if(oldp)
            oldp->decref();
        return *this;
    }

    CountedPtr& operator=(T *p)
    {
        if(p != _p)
        {
             T * const oldp = _p;
             _p = p;
             if(p)
                p->incref();
             if(oldp)
                 oldp->decref();
        }
        return *this;
    }

    T* operator->() const { return  _p; }
    T& operator* () const { return *_p; }

    bool operator!() const { return !_p; }

    // Safe for use in if statements
    operator const void*() const  { return _p; }

    template <typename U>
    const CountedPtr<U>& reinterpret() const { return *reinterpret_cast<const CountedPtr<U>*>(this); }

    template <typename U>
    CountedPtr<U>& reinterpret() { return *reinterpret_cast<CountedPtr<U>*>(this); }

    // if you use these, make sure you also keep a counted reference to the object!
    T* content () const { return _p; }

    bool operator<(const CountedPtr& ref) const { return _p < ref._p; }
    bool operator<=(const CountedPtr& ref) const { return _p <= ref._p; }
    bool operator==(const CountedPtr& ref) const { return _p == ref._p; }
    bool operator!=(const CountedPtr& ref) const { return _p != ref._p; }
    bool operator>=(const CountedPtr& ref) const { return _p >= ref._p; }
    bool operator>(const CountedPtr& ref) const { return _p > ref._p; }

    bool operator<(const T *p) const { return _p < p; }
    bool operator<=(const T *p) const { return _p <= p; }
    bool operator==(const T *p) const { return _p == p; }
    bool operator!=(const T *p) const { return _p != p; }
    bool operator>=(const T *p) const { return _p >= p; }
    bool operator>(const T *p) const { return _p > p; }

    typedef bool is_counted_ptr; // tag for some compile-time checks

private:

    T *_p;
};

typedef CountedPtr<Refcounted> CountedPtrAny;
typedef CountedPtr<const Refcounted> CountedCPtr;
