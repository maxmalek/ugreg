#pragma once

#include <vector>
#include <utility>
#include "variant.h"
#include "treemem.h"

// Accessor must be constructed for a DataTree, and is valid only for that DataTree!
class Accessor
{
private:
    std::vector<Var> a;
    TreeMem& mem;
    struct _Priv {}; // marker
public:
    ~Accessor()
    {
        for(size_t i = 0; i < a.size(); ++i)
            a[i].clear(mem);
    }

    template<typename ...T>
    Accessor(TreeMem& mem, T... args)
        : a(sizeof...(T)), mem(mem)
    {
        _pack(mem, a.data(), args...);
    }

    inline size_t size() const { return a.size(); }
    inline const Var& operator[](size_t i) const { return a[i]; }

private:
    inline Accessor(TreeMem& mem, size_t n, _Priv) : a(n), mem(mem) {}

    // only make this work for strings and ints
    static inline Var _makevar(TreeMem&    , u64 x) { return Var(x); }
    static inline Var _makevar(TreeMem& mem, const char *x) { return Var(mem, x); }

    static void _pack(TreeMem&, Var*)
    {
        // hurr durr
    }
    template<typename T>
    static void _pack(TreeMem& mem, Var* dst, const T& x)
    {
        *dst = _makevar(mem, x);
    }
    template<typename T, typename...Rest>
    static void _pack(TreeMem& mem, Var *dst, const T& x, Rest...rest)
    {
        *dst = _makevar(mem, x);
        _pack(mem, dst+1, rest...);
    }
};
