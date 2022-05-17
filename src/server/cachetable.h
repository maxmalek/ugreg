#pragma once

#include <vector>
#include <utility>
#include <shared_mutex>
#include <mutex>

#include "types.h"
#include "util.h"
#include "refcounted.h"

template<typename T>
class Hashed
{
public:
    typedef Hashed<T> Self;
    T obj;
    u32 hash;

    // Default ctor produces an invalid object
    Hashed()
        : hash(T::Hash(T()) + 1) // Make sure the hash is invalid, so this will never compare equal to anything
    {}
    Hashed(const T& o)
        : obj(o), hash(o.Hash(o))
    {}
    Hashed(T&& o) noexcept
        : obj(std::move(o)), hash(obj.Hash(obj))
    {}
    Hashed(const Hashed& o)
        : obj(o.obj), hash(o.hash)
    {}
    Hashed(Hashed&& o) noexcept
        : obj(std::move(o.obj)), hash(o.hash)
    {}

    Hashed& operator=(const Hashed& o)
    {
        obj = o.obj;
        hash = o.hash;
        return *this;
    }
    Hashed& operator=(Hashed&& o)
    {
        obj = std::move(o.obj);
        hash = o.hash;
        return *this;
    }
    Hashed& operator=(const T& o)
    {
        obj = o;
        hash = o.Hash(o);
        return *this;
    }
    Hashed& operator=(T&& o)
    {
        obj = std::move(o);
        hash = o.Hash(o);
        return *this;
    }

    inline bool operator==(const Self& o) const
    {
        return hash == o.hash && obj == o.obj;
    }
    inline bool operator!=(const Self& o) const
    {
        return hash != o.hash || !(obj == o.obj);
    }
};

// (slots x cols) grid. Slot is selected by hashing, cols is linearly probed.
// So we want to keep cols small (<= 32 entries; 8 should be a good value).
// Note that the cache table is designed to be hammered by many threads at the same time for reading and writing.
// Entries inside are wrapped in a CountedPtr<V> for exactly this reason:
// This makes sure that one thread can get a value, unlock, the next thread deletes that value,
// but the first one still has a counted ref to the object while it is in use.
template<typename K, typename V>
class CacheTable
{
public:
    typedef Hashed<K> Key;
    inline bool enabled() const { return _enabled; }
private:
    bool _enabled;
    size_t _cols; // power of 2
    u32 _rng;
    u32 _mask; // (power of 2) - 1
    mutable std::shared_mutex mutex;
    std::vector<Key> _keys;
    std::vector<CountedPtr<V> > _vals;

    // 16bit xorshift variant
    u32 _randomcol()
    {
        u32 s = _rng;
        s ^= (s << 1);
        s ^= (s >> 1);
        s ^= (s << 14);
        //s &= 0xffff;
        _rng = s;
        return s & (_cols - 1);
    }
public:
    CacheTable(u32 seed = 6581)
        : _enabled(false), _cols(0), _rng(seed + !seed), _mask(0)
    {
    }

    void resize(u32 rows, u32 cols)
    {
        _enabled = rows && cols;
        if(_enabled)
        {
            _cols = roundPow2(cols);
            _mask = (roundPow2(rows) - 1) | 1;

            const size_t N = (size_t(_mask) + 1) * cols;
            _keys.resize(N);
            _vals.resize(N);
        }
    }

    void clear()
    {
        const size_t N = _keys.size();
        const Key inval;
        V * const nil = NULL;
        std::unique_lock<std::shared_mutex> lock(mutex);
        for(size_t i = 0; i < N; ++i)
        {
            _keys[i] = inval;
            _vals[i] = nil;
        }
    }

    // puts into slot for key if exists, otherwise chooses random location
    void put(const Key& k, CountedPtr<V> v)
    {
        const size_t slot = k.hash & _mask;
        const size_t begin = slot * _cols;
        const size_t end = begin + _cols;
        size_t i = begin;

        std::unique_lock<std::shared_mutex> lock(mutex);

        // find key
        for ( ; i < end; ++i)
            if (k == _keys[i])
                goto put;
        // pick random
        i = begin + _randomcol();
    put:
        _keys[i] = k;
        _vals[i] = std::move(v);
    }

    // return ptr to first element that matches key, NULL if none found
    CountedPtr<V> get(const Key& k) const
    {
        const size_t slot = k.hash & _mask;
        size_t i = slot * _cols;
        const size_t end = i + _cols;

        std::shared_lock<std::shared_mutex> lock(mutex);

        for (; i < end; ++i)
            if (k == _keys[i])
                return _vals[i];
        return CountedPtr<V>();
    }


};



