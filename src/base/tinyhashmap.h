#pragma once

#include "containers.h"
#include <vector> // TODO KILL ME
#include <assert.h>

/* Put the HashHat on a vector to turn it into a rudimentary hashmap
Optimized for:
- StrRef as key. This is hardcoded.
- External allocator
- Little storage space overhead
- Very fast lookup
- Fast insert
- Deleting individual keys is not possible
- But clearing the entire thing is
- Assume that keys can not be constructed from the value, and non-copyable values.
- Keys are stored in a bucket stucture
- Value backing memory is a simple linear vector and can be accessed via pointer + size
  (without any holes in between)
-> Trivial iteration over values
- ... So all this thing does is to provide an index for the backing vector

There is just a slight problem. A std::(unordered_)map operates on pairs,
which isn't great but not really a problem for the data structure per se,
but the map's iterator really wants a pair<> REFERENCE type,
which implies they HAVE to be located as adjacent pairs in memory.
(It might be possible to work around this using a std::reference_wrapper but let's not go there.)

Mandating std::pair prevents many optimizations and is dumb not only for cache & perf reasons,
and can't be worked around reliably while keeping the established iterator API.
That's why my iterators have key() and value() methods instead of overloaded operator -> and *.
Unfortunately this breaks C++11 range-for, but since the code doesn't use it (old C++03 habits
die hard) I don't particularly care about that.
(And IMHO it looks cleaner this way than it.first and it.second...)
*/

// put #if 0 here to use std::unordered_map under the hood instead of the custom thing
#if 1

template<typename SZ>
class HashHatKeyStore
{
public:
    typedef BlockAllocator Allocator;
    typedef SZ size_type;

    struct Bucket
    {
        struct Policy
        {
            typedef Allocator Allocator;
            template<typename Bucket> // This works around Bucket being not defined just yet
            inline static void OnDestroy(Allocator& mem, Bucket& v)
            {
                v.dealloc(mem);
            }
        };

        std::vector<StrRef> keys;
        std::vector<SZ> indices; // never contains 0

        inline SZ size() const { return keys.size(); }
        inline void clear(Allocator& mem) { keys.clear(); indices.clear(); }
        inline void swap(Bucket& o) { keys.swap(o.keys); indices.swap(o.indices); }

        SZ& pushNewKey(StrRef k)
        {
            keys.push_back(k);
            indices.push_back(0);
            return indices.back();
        }

        void dealloc(Allocator& mem)
        {
            clear(mem);
            // TODO: free
        }

        // iterator over a single bucket
        template<typename T, typename B>
        struct iterator_T
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;

            iterator_T(iterator_T&&) = default;
            iterator_T& operator=(iterator_T&&) = default;
            iterator_T& operator=(const iterator_T&) = default;

            iterator_T(B *b, SZ idx) // iterator into some storage
                : _idx(idx), _b(b) {}

            iterator_T(B *b) // iterator to end
                : _idx(b->size()), _b(b) {}

            iterator_T() // empty iterator
                : _idx(0), _b(NULL) {}

            //iterator_T(const iterator_T<T, Bucket const>& it) // construct fron non-const
            //    : _idx(it._idx), _b(it._b) {}

            iterator_T(const iterator_T& o) // copy
                : _idx(o._idx), _b(o._b) {}

            iterator_T& operator++() { assert(_idx < _b->size()); ++_idx; return *this; }
            iterator_T operator++(int) { iterator_T tmp = *this; ++(*this); return tmp; }
            friend bool operator== (const iterator_T& a, const iterator_T& b)
            //{ assert(a._b == b._b); return a._idx == b._idx; };
            { return a._b == b._b && a._idx == b._idx; };
            friend bool operator!= (const iterator_T& a, const iterator_T& b)
            //{ assert(a._b == b._b); return a._idx != b._idx; };
            { return a._b != b._b || a._idx != b._idx; };

            StrRef key() const     { return _b->keys[_idx]; }
            T& value()             { return _b->indices[_idx]; }
            const T& value() const { return _b->indices[_idx]; }
            bool _done() const { return _idx >= _b->size(); }

            SZ _idx; // index in bucket
            B * _b; // current bucket
        };

        //typedef iterator_T<size_t, Bucket> iterator;
        typedef iterator_T<SZ const, Bucket const> const_iterator;
        
        //iterator begin() { return iterator(this, 0); }
        //iterator end()   { return iterator(this); }

        const_iterator begin() const { return const_iterator(this, 0); }
        const_iterator end()   const { return const_iterator(this); }
    };

    typedef LVector<Bucket, u32, typename Bucket::Policy> Storage;

    HashHatKeyStore()
        : _mask(SZ(-1)) // This is intended to overflow once we insert the first element
    {
    }

    HashHatKeyStore(Allocator& mem, SZ initialbuckets = 4) // must be power of 2
        : _mask(initialbuckets - 1)
    {
        assert(initialbuckets);
        _buckets.resize(mem, initialbuckets);
    }

    ~HashHatKeyStore()
    {
    }

    HashHatKeyStore(HashHatKeyStore&& o) noexcept
        : _buckets(std::move(o._buckets))
        , _mask(o._mask)
    {
        o._mask = -1;
    }

    HashHatKeyStore(const HashHatKeyStore&) = delete;
    HashHatKeyStore& operator=(const HashHatKeyStore&) = delete;
    HashHatKeyStore& operator=(HashHatKeyStore&&) noexcept = delete;

    // returns 0 if not found
    const size_t getIndex(StrRef k) const
    {
        _Validkey(k);
        if(_buckets.size())
        {
            const Bucket& b = _getbucket(k);
            const SZ N = b.size();
            for(size_t i = 0; i < N; ++i)
                if(k == _Validkey(b.keys[i]))
                    return b.indices[i];
        }
        return 0;
    }
    
    inline static StrRef _Validkey(StrRef k)
    {
        assert(k);
        return k;
    }

    // return location of new index to write. if it's 0, the index is new and must be updated.
    // Do NOT write 0 there!
    SZ& insertIndex(Allocator& mem, SZ cursize, StrRef k)
    {
        _Validkey(k);
        Bucket *b;
        // Load factor: in average 8 elements ber bucket
        if((cursize >> 3u) >= SZ(_mask + 1)) // CAUTION: This uses _mask overflowing when inserting the first element
        {
            SZ newsize = _buckets.size() * 2;
            SZ mask = resize(mem, newsize < 4 ? 4 : newsize);
            b = &_buckets[k & mask]; // bucket vector was realloc'd, fix ptr
        }
        else
            b = &_getbucket(k);

        const SZ N = b->size();
        const StrRef* const ka = b->keys.data();
        for (SZ i = 0; i < N; ++i)
            if (k == _Validkey(ka[i]))
                return b->indices[i];

        return b->pushNewKey(k);
    }

    SZ resize(Allocator& mem, SZ newsize)
    {
        const SZ B = _buckets.size();
        _buckets.resize(mem, newsize);
        SZ newmask = newsize - 1;
        Bucket tmp;
        for(SZ j = 0; j < B; ++j)
        {
            tmp.clear(mem);
            Bucket& src = _buckets[j];
            tmp.swap(src);
            const SZ N = tmp.size();
            for(SZ i = 0; i < N; ++i)
            {
                const SZ bidx = tmp.keys[i] & newmask;
                assert(bidx < _buckets.size());
                Bucket& dst = _buckets[bidx];
                dst.keys.push_back(tmp.keys[i]);
                dst.indices.push_back(tmp.indices[i]);
            }
        }
        _mask = newmask;
        return newmask;
    }

    void clear(Allocator& mem)
    {
        for(size_t i = 0; i < _buckets.size(); ++i)
            _buckets[i].clear(mem);
    }

    void dealloc(Allocator& mem)
    {
        clear(mem);
        _buckets.dealloc(mem);
        _mask = SZ(-1);
    }

    // iterator over multiple buckets
    template<typename T, typename B>
    struct iterator_T
    {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;

        iterator_T(iterator_T&&) = default;
        iterator_T& operator=(iterator_T&&) = default;
        iterator_T& operator=(const iterator_T&) = default;

        iterator_T(const typename B::const_iterator& it, B *end)
            : _it(it), _end(end) {}

        // construct from non-const
        //iterator_T(const iterator_T<T, Bucket, Bucket::iterator>& it)
        //    : _it(it._it), _end(it._end) {}

        iterator_T() // empty iterator
            : _end(NULL) {}

        iterator_T(B *b, size_t n)
            : _it(b, 0), _end(b + n) {}

        iterator_T(const iterator_T& o) // copy
            : _it(o._it), _end(o._end) {}

        iterator_T& _advanceIfEmpty()
        {
            if (_it._done())
            {
                assert(_it._b < _end);
                do
                {
                    ++_it._b;
                    _it._idx = 0;
                }
                while (_it._b < _end && _it._done()); // skip empty buckets
            }
            return *this;
        }

        iterator_T& operator++()
        {
            assert(_it._b < _end);
            ++_it;
            return _advanceIfEmpty();
        }
        iterator_T operator++(int) { iterator_T tmp = *this; ++(*this); return tmp; }
        friend bool operator== (const iterator_T& a, const iterator_T& b)
        {
            assert(a._end == b._end); return a._it == b._it;
        };
        friend bool operator!= (const iterator_T& a, const iterator_T& b)
        {
            assert(a._end == b._end); return a._it != b._it;
        };

        StrRef key() const { return _it.key(); }
        T value() { return _it.value(); }
        const T value() const { return _it.value(); }

    private:
        typename B::const_iterator _it; // iterator into current bucket
        const B *_end; // one past last bucket
    };

    //typedef iterator_T<size_t, Bucket> iterator;
    typedef iterator_T<size_t const, Bucket const> const_iterator;

    const_iterator begin() const
    {
        return const_iterator(_buckets.data(), _buckets.size())._advanceIfEmpty();
    }
    const_iterator end() const
    {
        return const_iterator(_buckets.data() + _buckets.size(), 0);
    }

    //const_iterator begin() const { return const_iterator(); }
    //const_iterator end()   const { return const_iterator(); }

private:

    inline Bucket& _getbucket(StrRef k)
    {
        const size_t idx = k & _mask;
        assert(idx < _buckets.size());
        return _buckets[idx];
    }

    inline const Bucket& _getbucket(StrRef k) const
    {
        const size_t idx = k & _mask;
        assert(idx < _buckets.size());
        return _buckets[idx];
    }
    
    Storage _buckets;
    SZ _mask;
};

template<typename Vec>
class HashHat
{
public:
    typedef HashHatKeyStore<u32> KS;
    typedef typename Vec::value_type value_type;
    typedef typename KS::Allocator Allocator;
    struct InsertResult
    {
        value_type& ref;
        bool newly_inserted;
    };
    HashHat()
    {
    }
    ~HashHat()
    {
    }

    HashHat(HashHat&& o) noexcept
        : ks(std::move(o.ks))
    {
    }

    HashHat(const HashHat&) = delete;
    HashHat& operator=(const HashHat&) = delete;
    HashHat& operator=(HashHat&&) noexcept = delete;

    InsertResult _insert_always(typename KS::size_type& dst, Vec& vec, Allocator& mem, StrRef k, value_type&& v)
    {
        typename Vec::value_type& ins = vec.push_back(mem, std::move(v));
        dst = vec.size(); // store size *after* inserting, that is always != 0
        InsertResult ret{ ins, true };
        return ret;
    }

    InsertResult insert(Vec& vec, Allocator& mem, StrRef k, value_type&& v)
    {
        typename KS::size_type& dst = ks.insertIndex(mem, vec.size(), k);
        if(dst)
        {
            value_type& vdst = vec[dst - 1];
            vdst = std::move(v);
            InsertResult ret { vdst, false };
            return ret;
        }
        return _insert_always(dst, vec, mem, k, std::move(v));
    }

    InsertResult insert_new(Vec& vec, Allocator& mem, StrRef k)
    {
        typename KS::size_type& dst = ks.insertIndex(mem, vec.size(), k);
        if (dst)
        {
            value_type& vdst = vec[dst - 1];
            InsertResult ret { vdst, false };
            return ret;
        }
        return _insert_always(dst, vec, mem, k, std::move(value_type()));
    }

    value_type *getp(Vec& vec, StrRef k)
    {
        const size_t idx = ks.getIndex(k);
        return idx ? &vec[idx - 1] : NULL;
    }

    const value_type* getp(const Vec& vec, StrRef k) const
    {
        const size_t idx = ks.getIndex(k);
        return idx ? &vec[idx - 1] : NULL;
    }

    template<typename T, typename BaseT, bool IsConst>
    struct iterator_T
    {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;

        iterator_T(const iterator_T&) = default;
        iterator_T(iterator_T&&) = default;
        iterator_T& operator=(iterator_T&&) = default;
        iterator_T& operator=(const iterator_T&) = default;

        iterator_T() : _a(NULL) {}

        iterator_T(T* a, KS::const_iterator it) // iterator into valid storage
            : _it(it), _a(a)
        {
        }

        template<bool WasConst, class = std::enable_if_t<IsConst || !WasConst> >
        iterator_T(const iterator_T<BaseT, BaseT, WasConst>& o)
            : _it(o._it), _a(o._a)
        {
        }

        iterator_T& operator++() { ++_it; return *this; }
        iterator_T operator++(int) { iterator_T tmp = *this; ++(*this); return tmp; }
        friend bool operator== (const iterator_T& a, const iterator_T& b)
        {
            assert(a._a == b._a); return a._it == b._it;
        };
        friend bool operator!= (const iterator_T& a, const iterator_T& b)
        {
            assert(a._a == b._a); return a._it != b._it;
        };

        StrRef key() const { return _it.key(); }
        T& value() { assert(_it.value()); return _a[_it.value() - 1]; }
        const T& value() const { assert(_it.value()); return _a[_it.value() - 1]; }

        KS::const_iterator _it;
        T *_a;
    };

    typedef iterator_T<value_type const, value_type, true> const_iterator;

    typedef iterator_T<value_type, value_type, false> iterator;
    //class iterator : public const_iterator
    //{};


    iterator begin(value_type *a) { return iterator(a, ks.begin()); }
    iterator end(value_type *a)   { return iterator(a, ks.end()); }

    const_iterator begin(const value_type *a) const { return const_iterator(a, ks.begin()); }
    const_iterator end(const value_type *a)   const { return const_iterator(a, ks.end()); }

    void clear(Allocator& mem) { ks.clear(mem); }
    void dealloc(Allocator& mem) { ks.dealloc(mem); }

private:
   KS ks;
};

template<typename T, typename Policy = DefaultPolicy<T> >
class TinyHashMap
{
    typedef LVector<T, u32, Policy> TVec;
    typedef HashHat<TVec> HH;
    typedef typename HH::Allocator Allocator;
    HashHat<TVec> _hh;
    TVec _vec;

public:
    typedef typename HH::iterator iterator;
    typedef typename HH::const_iterator const_iterator;
    typedef typename HH::InsertResult InsertResult;

    TinyHashMap(const TinyHashMap&) = delete;
    TinyHashMap& operator=(const TinyHashMap&) = delete;

    TinyHashMap()
    {
    }
    TinyHashMap(Allocator& mem, size_t = 0)
    {
    }
    ~TinyHashMap()
    {
    }
    TinyHashMap(TinyHashMap&& o) noexcept
        : _hh(std::move(o._hh)), _vec(std::move(o._vec))
    {
    }
    TinyHashMap& operator=(TinyHashMap&& o) noexcept = delete;
    /*{
        assert(false && "clear me!");
        if(this != &o)
        {
            _hh = std::move(o._hh);
            _vec = std::move(o._vec);
        }
        return *this;
    }*/

    InsertResult insert(Allocator& mem, StrRef k, T&& v)
    {
        return _hh.insert(_vec, mem, k, std::move(v));
    }

    InsertResult insert_new(Allocator& mem, StrRef k)
    {
        return _hh.insert_new(_vec, mem, k);
    }

    T* getp(StrRef k)
    {
        return _hh.getp(_vec, k);
    }
    const T* getp(StrRef k) const
    {
        return _hh.getp(_vec, k);
    }

    iterator begin() { return _hh.begin(_vec.data()); }
    iterator end()   { return _hh.end(_vec.data()); }

    const_iterator begin() const { return _hh.begin(_vec.data()); }
    const_iterator end()   const { return _hh.end(_vec.data()); }

    size_t size() const { return _vec.size(); }
    bool empty() const { return _vec.empty(); }
    void clear(Allocator& mem)
    {
        //for(size_t i = 0; i < _vec.size(); ++i)
        //    _vec[i].clear(mem);
        _vec.clear(mem);
        _hh.clear(mem);
    }

    void dealloc(Allocator& mem)
    {
        _vec.dealloc(mem);
        _hh.dealloc(mem);
    }

    inline T& at(Allocator& mem, StrRef k)
    {
        return insert_new(mem, k).ref;
    }
};

#else


#include <unordered_map>

// For testing -- wraps std::unordered_map to the new API
template<typename T, typename Policy = DefaultPolicy<T> >
class TinyHashMap
{
    typedef std::unordered_map<StrRef, T> Storage;
    typedef BlockAllocator Allocator;
public:
    TinyHashMap() {}
    TinyHashMap(Allocator&, size_t = 0)
    {
    }
    TinyHashMap(TinyHashMap&& o) noexcept
        : _map(std::move(o._map))
    {
    }
    TinyHashMap& operator=(TinyHashMap&& o) noexcept = delete;
    /*{
        _map = std::move(o._map);
    }*/

    TinyHashMap(const TinyHashMap&) = delete;
    TinyHashMap& operator=(const TinyHashMap&) = delete;

    struct InsertResult
    {
        T& ref;
        bool newly_inserted;
    };

    InsertResult insert(Allocator&, StrRef k, T&& v)
    {
        std::pair<StrRef, T> p(k, std::move(v));
        auto ins = _map.insert(std::move(p));
        InsertResult ret { ins.first->second, ins.second };
        return ret;
    }

    InsertResult insert_new(Allocator&, StrRef k)
    {
        auto ins = _map.try_emplace(k);
        InsertResult ret { ins.first->second, ins.second };
        return ret;
    }

    T *getp(StrRef k)
    {
        auto it = _map.find(k);
        return it != _map.end() ? &it->second : NULL;
    }
    const T* getp(StrRef k) const
    {
        auto it = _map.find(k);
        return it != _map.end() ? &it->second : NULL;
    }

    template<typename Elem, typename BaseIter>
    struct _iterator_T
    {
        typedef _iterator_T<Elem, BaseIter> Self;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;

        _iterator_T() {}
        _iterator_T(BaseIter&& it) : _it(it) {}
        ~_iterator_T() {}

        // construct const from non-const
        _iterator_T(const _iterator_T<T, typename Storage::iterator>& it)
            : _it(it._it) {}

        // we use this instead of the ->first, ->second nonsense
        inline StrRef key() { return _it->first; }
        inline Elem& value() { return _it->second; }
        inline const Elem& value() const { return _it->second; }

        Self& operator++() { ++_it; return *this; }
        Self operator++(int) { iterator tmp = *this; ++(*this); return tmp; }
        friend bool operator== (const Self& a, const Self& b)
        {
            return a._it == b._it;
        };
        friend bool operator!= (const Self& a, const Self& b)
        {
            return a._it != b._it;
        };

        BaseIter _it;
    };

    typedef _iterator_T<T, typename Storage::iterator> iterator;
    typedef _iterator_T<T const, typename Storage::const_iterator> const_iterator;

    iterator begin() { return iterator(_map.begin()); }
    iterator end() { return iterator(_map.end()); }
    const_iterator begin() const { return const_iterator(_map.cbegin()); }
    const_iterator end() const { return const_iterator(_map.cend()); }

    size_t size() const { return _map.size(); }
    bool empty() const { return _map.empty(); }
    void clear(Allocator& mem)
    {
        for(Storage::iterator it = _map.begin(); it != _map.end(); ++it)
            Policy::OnDestroy(mem, it->second);
        _map.clear();
    }
    void dealloc(Allocator& mem) { clear(mem); }
    //T& operator[](StrRef k) { return _map[k]; } // can't support this one
    T& at(Allocator&, StrRef k) { return _map[k]; }
    void swap(TinyHashMap& o) { _map.swap(o); }


private:
    Storage _map;
};

#endif

void tinyhashmap_api_test();
