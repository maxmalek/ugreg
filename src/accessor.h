#pragma once

#include <vector>
#include "variant.h"

class Accessor
{
private:
    std::vector<Var> a;
    struct _Priv {}; // marker
public:

    template<typename ...T>
    Accessor(T... args)
        : Accessor(Pack(args...))
    {
    }

    template<typename ...T>
    static Accessor Pack(T... args)
    {
        constexpr size_t n = sizeof...(T);
        Accessor acc(n, _Priv());
        _pack(acc.a.data(), args...);
        return acc;
    }

    inline size_t size() const { return a.size(); }
    inline const Var& operator[](size_t i) const { return a[i]; }

private:
    inline Accessor(size_t n, _Priv) : a(n) {}

    // only make this work for strings and ints
    static inline Var _makevar(u64 x) { return x; }
    static inline Var _makevar(const char *x) { return x; }

    static void _pack(Var* dst)
    {
        // hurr durr
    }
    template<typename T>
    static void _pack(Var* dst, const T& x)
    {
        *dst = _makevar(x);
    }
    template<typename T, typename...Rest>
    static void _pack(Var *dst, const T& x, Rest...rest)
    {
        *dst = _makevar(x);
        _pack(dst+1, rest...);
    }
};
