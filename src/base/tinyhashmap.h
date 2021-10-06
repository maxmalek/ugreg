#pragma once

#include "containers.h"
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
            inline static void OnDestroy(Allocator& mem, Bucket& b)
            {
                b.dealloc(mem);
            }
        };

        StrRef *_keys;
        SZ *_indices; // never contains 0
        SZ _cap;
        SZ _sz; // both arrays have the same length and capacity

        Bucket() : _keys(NULL), _indices(NULL), _cap(0), _sz(0) {}

        Bucket(const Bucket&) = delete;
        Bucket& operator=(const Bucket&) = delete;
        Bucket& operator=(Bucket&&) = delete;

        Bucket(Bucket&& o) noexcept
            : _keys(o._keys), _indices(o._indices), _cap(o._cap), _sz(o._sz)
        {
            o._keys = NULL;
            o._indices = NULL;
            o._cap = 0;
            o._sz = 0;
        }

        inline const StrRef *keys() const { return _keys; }
        inline const SZ *indices() const { return _indices; }
        inline SZ *indicesWritable() { return _indices; }

        inline SZ size() const { return _sz; }
        inline void clear(Allocator& mem) { _sz = 0; }
        inline void swap(Bucket& o) noexcept
        {
            std::swap(_keys, o._keys);
            std::swap(_indices, o._indices);
            std::swap(_cap, o._cap);
            std::swap(_sz, o._sz);
        }

        SZ& _pushKey(Allocator& mem, StrRef k, SZ idx)
        {
            assert(!_cap == !_keys);
            StrRef* akeys = _keys;
            SZ *aidx = _indices;
            const SZ i = _sz++;
            if(i == _cap)
            {
                SZ newcap = _cap + 4; // the hashmap is going to redistribute keys eventually, don't go too big
                akeys = (StrRef*)mem.Realloc(akeys, _cap * sizeof(*_keys), newcap * sizeof(*_keys));
                aidx = (SZ*)mem.Realloc(aidx, _cap * sizeof(*_indices), newcap * sizeof(*_indices));
                _keys = akeys;
                _indices = aidx;
                _cap = newcap;
            }
            akeys[i] = k;
            aidx[i] = idx;
            return aidx[i];
        }

        inline SZ& pushNewKey(Allocator& mem, StrRef k)
        {
            return _pushKey(mem, k, 0);
        }

        void dealloc(Allocator& mem)
        {
            assert(!_cap == !_keys);
            mem.Free(_keys, _cap * sizeof(*_keys));
            mem.Free(_indices, _cap * sizeof(*_indices));
            _keys = NULL;
            _indices = NULL;
            _cap = 0;
            _sz = 0;
        }

        // iterator over a single bucket
        template<typename T, typename B>
        struct iterator_T
        {
            iterator_T(iterator_T&&) = default;
            iterator_T& operator=(iterator_T&&) = default;
            iterator_T& operator=(const iterator_T&) = default;

            iterator_T(B *b, SZ idx) // iterator into some storage
                : _idx(idx), _b(b) {}

            iterator_T(B *b) // iterator to end
                : _idx(b->size()), _b(b) {}

            iterator_T() // empty iterator
                : _idx(0), _b(NULL) {}

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

            StrRef key() const     { assert(_idx < _b->_sz); return _b->_keys[_idx]; }
            T& value()             { assert(_idx < _b->_sz); return _b->_indices[_idx]; }
            const T& value() const { assert(_idx < _b->_sz); return _b->_indices[_idx]; }
            bool _done() const { return _idx >= _b->size(); }

            SZ _idx; // index in bucket
            B * _b; // current bucket
        };

        typedef iterator_T<SZ const, Bucket const> const_iterator;

        const_iterator begin() const { return const_iterator(this, 0); }
        const_iterator end()   const { return const_iterator(this); }
    };

    typedef LVector<Bucket, u32, typename Bucket::Policy> Storage;

    HashHatKeyStore()
    {
    }

    HashHatKeyStore(Allocator& mem, SZ initialbuckets = 4) // must be power of 2
        : _buckets(mem, initialbuckets)
    {
        assert(initialbuckets);
    }

    ~HashHatKeyStore()
    {
    }

    HashHatKeyStore(HashHatKeyStore&& o) noexcept
        : _buckets(std::move(o._buckets))
    {
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
            const StrRef* const ka = b.keys();
            for(size_t i = 0; i < N; ++i)
                if(k == _Validkey(ka[i]))
                    return b.indices()[i];
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
        if((cursize >> 3u) >= _buckets.size())
        {
            SZ newsize = _buckets.size() * 2;
            SZ mask = resize(mem, newsize < 4 ? 4 : newsize);
            b = &_buckets[k & mask]; // bucket vector was realloc'd, fix ptr
        }
        else
            b = &_getbucket(k);

        const SZ N = b->size();
        const StrRef* const ka = b->keys();
        for (SZ i = 0; i < N; ++i)
            if (k == _Validkey(ka[i]))
                return b->indicesWritable()[i];

        return b->pushNewKey(mem, k);
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
            const StrRef *tkeys = tmp.keys();
            const SZ *tidx = tmp.indices();
            for(SZ i = 0; i < N; ++i)
            {
                const SZ bidx = tkeys[i] & newmask;
                assert(bidx < _buckets.size());
                Bucket& dst = _buckets[bidx];
                dst._pushKey(mem, tkeys[i], tidx[i]);
            }
        }
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
    }

    // iterator over multiple buckets
    template<typename T, typename B>
    struct iterator_T
    {
        iterator_T(iterator_T&&) = default;
        iterator_T& operator=(iterator_T&&) = default;
        iterator_T& operator=(const iterator_T&) = default;

        iterator_T(const typename B::const_iterator& it, B *end)
            : _it(it), _end(end) {}

        iterator_T() // empty iterator
            : _end(NULL) {}

        iterator_T(B *b, size_t n)
            : _it(b, 0), _end(b + n) {}

        iterator_T(const iterator_T& o) // copy
            : _it(o._it), _end(o._end) {}

        iterator_T& _advanceIfEmpty()
        {
            while (_it._b < _end && _it._done()) // skip empty buckets
            {
                ++_it._b;
                _it._idx = 0;
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

    typedef iterator_T<size_t const, Bucket const> const_iterator;

    const_iterator begin() const
    {
        return const_iterator(_buckets.data(), _buckets.size())._advanceIfEmpty();
    }
    const_iterator end() const
    {
        return const_iterator(_buckets.data() + _buckets.size(), 0);
    }

private:

    inline SZ _mask() const { return _buckets.size() - 1; }

    inline Bucket& _getbucket(StrRef k)
    {
        const size_t idx = k & _mask();
        assert(idx < _buckets.size());
        return _buckets[idx];
    }

    inline const Bucket& _getbucket(StrRef k) const
    {
        const size_t idx = k & _mask();
        assert(idx < _buckets.size());
        return _buckets[idx];
    }

    Storage _buckets;
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

    iterator begin(value_type *a) { return iterator(a, ks.begin()); }
    iterator end(value_type *a)   { return iterator(a, ks.end()); }

    const_iterator begin(const value_type *a) const { return const_iterator(a, ks.begin()); }
    const_iterator end(const value_type *a)   const { return const_iterator(a, ks.end()); }

    void clear(Allocator& mem) { ks.clear(mem); }
    void dealloc(Allocator& mem) { ks.dealloc(mem); }

private:
   KS ks;
};

template<typename T, typename Policy = ContainerDefaultPolicy<T> >
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
template<typename T, typename Policy = ContainerDefaultPolicy<T> >
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
