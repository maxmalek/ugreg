#pragma once

#include "treemem.h"
#include <vector>
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
#if 0

class HashHatKeyStore
{
public:
    struct Bucket
    {
        std::vector<StrRef> keys;
        std::vector<size_t> indices; // never contains 0

        inline size_t size() const { return keys.size(); }
        inline void clear() { keys.clear(); indices.clear(); }
        inline void swap(Bucket& o) { keys.swap(o.keys); indices.swap(o.indices); }

        struct iterator
        {
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::pair<StrRef, size_t>;
            using pointer = const std::pair<StrRef, size_t>*;
            using reference = const std::pair<StrRef, size_t>&;

            iterator(const Bucket *b, size_t idx) // iterator into valid storage
                : _idx(idx), _b(b)
            {
                if(idx < b->size())
                    assign(idx);
            }

            iterator(const Bucket* b) // iterator to end
                : _idx(_b->size()), _b(b)
            {
            }

            reference operator*() const { return mypair; }
            pointer operator->() { return &mypair; }
            iterator& operator++() { assign(_idx++); return *this; }
            iterator operator++(int) { iterator tmp = *this; ++(*this); return tmp; }
            friend bool operator== (const iterator& a, const iterator& b)
            { assert(a._b == b._b); return a._idx == b._idx; };
            friend bool operator!= (const iterator& a, const iterator& b)
            { assert(a._b == b._b); return a._idx != b._idx; };

            void assign(size_t idx)
            {
                mypair.first = _b->keys[idx];
                mypair.second = _b->indices[idx];
            }
        private:
            value_type mypair;
            size_t _idx; // index in bucket
            const Bucket *_b;
        };
        
        iterator begin() { return iterator(this, 0); }
        iterator end() { return iterator(this); }

    };

    typedef std::vector<Bucket> Storage;

private:
    typedef Storage::const_iterator bucket_iterator;
    typedef std::pair<StrRef, size_t > PairType;



    typedef iterator_base<const Bucket> const_iterator;

public:

    HashHatKeyStore(size_t initialbuckets = 1) // must be power of 2
        : _mask(initialbuckets - 1)
    {
        _buckets.resize(initialbuckets);
    }

    ~HashHatKeyStore()
    {
        _buckets.clear();
    }

    // returns 0 if not found
    const size_t getIndex(StrRef k) const
    {
        const Bucket& b = _getbucket(k);
        const size_t N = b.size();
        for(size_t i = 0; i < N; ++i)
            if(k == b.keys[i])
                return b.indices[i];
        return 0;
    }

    // return location of new index to write. if it's 0, the index is new and must be updated.
    // Do NOT write 0 there!
    size_t& insertIndex(TreeMem& mem, StrRef k)
    {
        Bucket& b = _getbucket(k);
        const size_t N = b.size();
        for (size_t i = 0; i < N; ++i)
            if (k == b.keys[i])
                return b.indices[i];

        b.keys.push_back(k);
        b.indices.push_back(0);
        return b.indices.back();
    }

    void resize(TreeMem& m, size_t newsize)
    {
        const size_t B = _buckets.size();
        _buckets.resize(newsize);
        size_t newmask = newsize - 1;
        Bucket tmp;
        for(size_t j = 0; j < B; ++j)
        {
            tmp.clear();
            Bucket& src = _buckets[j];
            tmp.swap(src);
            const size_t N = tmp.size();
            for(size_t i = 0; i < N; ++i)
            {
                const size_t bidx = tmp.keys[i] & newmask;
                Bucket& dst = _buckets[bidx];
                dst.keys.push_back(tmp.keys[i]);
                dst.indices.push_back(tmp.indices[i]);
            }
        }
        _mask = newmask;
    }

    void clear()
    {
        for(size_t i = 0; i < _buckets.size(); ++i)
            _buckets[i].clear();
        _buckets.clear();
    }

private:

    inline Bucket& _getbucket(StrRef k)
    {
        return _buckets[k & _mask];
    }

    inline const Bucket& _getbucket(StrRef k) const
    {
        return _buckets[k & _mask];
    }
    
    size_t _mask;
    Storage _buckets;
};

template<typename Vec>
class HashHat
{
public:
    typedef typename Vec::value_type value_type;
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

    InsertResult insert(Vec& vec, TreeMem& mem, StrRef k, value_type&& v)
    {
        size_t& dst = ks.insertIndex(mem, k);
        if(dst)
        {
            value_type vdst = vec[dst];
            vdst = std::move(v);
            InsertResult ret { vdst, false };
        }

        vec.push_back(std::move(v));
        dst = vec.size(); // store size+1, that is always != 0
        InsertResult ret{ vec.back(), true };
        return ret;
    }

    value_type *getp(Vec& vec, StrRef k)
    {
        const size_t idx = ks.getIndex(k);
        return idx ? &vec[idx - 1] : NULL;
    }

private:
    HashHatKeyStore ks;

};

template<typename T>
class TinyHashMap : private HashHat<std::vector<T> >
{
public:
    TinyHashMap()
    {
    }
    ~TinyHashMap()
    {
    }

    InsertResult insert(TreeMem& mem, StrRef k, value_type&& v)
    {
        return this->insert(vec, mem, k, std::move(v));
    }

    typedef iterator


private:
    std::vector vec;
};

#else


#include <unordered_map>

// For testing -- wraps std::unordered_map to the new API
template<typename T>
class TinyHashMap
{
    typedef std::unordered_map<StrRef, T> Storage;
public:
    TinyHashMap(TreeMem&, size_t = 0)
    {
    }
    TinyHashMap(TinyHashMap&& o) noexcept
        : _map(std::move(o._map))
    {
    }
    TinyHashMap& operator=(TinyHashMap&& o) noexcept
    {
        _map = std::move(o._map);
    }

    TinyHashMap(const TinyHashMap&) = delete;
    TinyHashMap& operator=(const TinyHashMap&) = delete;

    struct InsertResult
    {
        T& ref;
        bool newly_inserted;
    };

    InsertResult insert(TreeMem&, StrRef k, T&& v)
    {
        std::pair<StrRef, T> p(k, std::move(v));
        auto ins = _map.insert(std::move(p));
        InsertResult ret { ins.first->second, ins.second };
        return ret;
    }

    InsertResult insert_new(TreeMem&, StrRef k, T&& v)
    {
        auto ins = _map.try_emplace(k, std::move(v));
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
    void clear(TreeMem&) { _map.clear(); }
    T& operator[](StrRef k) { return _map[k]; }


private:
    Storage _map;
};

#endif
