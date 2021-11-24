#pragma once

#include "variant.h"
#include "treemem.h"
#include <assert.h>

// Example functor to iterate tree:
/*
struct WriterFunctor : public ConstTreeIterFunctor
{
    // Var was encountered. If it's a container, eventually End*() will be called.
    void operator()(VarCRef v) {}

    void EndArray(VarCRef v) {}       // finished iterating over array
    void EndObject(VarCRef v) {}      // finished iterating over map
    void Key(const char *k, size_t len) {} // encountered a map key (op() will be called next)
};
*/


struct ConstTreeIterFunctor
{
    typedef VarCRef VarWrapType;
    typedef const Var* VarPtrType;
    typedef const Var& VarRefType;
    typedef Var::Map::Iterator VarIterType;
};

struct MutTreeIterFunctor
{
    typedef VarRef VarWrapType;
    typedef Var* VarPtrType;
    typedef Var& VarRefType;
    typedef Var::Map::MutIterator VarIterType;
};

// Pass functor to iterate over all tree nodes.
// Return true from functor to recurse into object/array,
// return false to not recurse.
template<typename Functor>
void treeIter_T(Functor& func, const typename Functor::VarWrapType src)
{
    typedef typename Functor::VarWrapType Wrap;
    typedef typename Functor::VarPtrType Ptr;
    typedef typename Functor::VarRefType Ref;
    typedef typename Functor::VarIterType Iter;

    assert(src.v);

    if (!func(src) || !src.isContainer())
        return;

    // This would be simpler and easier to just write out recursively,
    // but this could then crash due to a stack overflow in pathological cases.
    // So we do the same as rapidjson does when reading:
    // Implement our own stack and instead of recursion, iterate.
    struct State
    {
        State(Ref x) : v(&x), ty(x.type())
        {
            switch (ty)
            {
            case Var::TYPE_ARRAY: it.array = 0; break;
            case Var::TYPE_MAP: it.map = x.u.m->begin(); break;
            default: assert(false); // State should never get constructed for non-containers
            }
        }
        ~State() {}
        Ptr v;
        Var::Type ty; // array or map
        struct // Could be a union but C++11 rules make this more nasty to define than it should be. Lazyness wins.
        {
            size_t array;
            Iter map;
        } it;
    };

    // Invariants:
    // - Only containers end up in the stack
    // - If a container ends up in the stack, it's not empty
    std::vector<State> stk;

    State s(*src.v);
    goto next;

    while (!stk.empty())
    {
        s = std::move(stk.back());
        stk.pop_back();
    next:
        // It's a container that is not empty. Start to emit its contents.
        switch (s.ty)
        {
            case Var::TYPE_ARRAY:
            {
                Ptr const a = s.v->array_unsafe();
                const size_t sz = s.v->_size();
                while (s.it.array < sz)
                {
                    Ref vv = a[s.it.array];
                    ++s.it.array;
                    if (func(Wrap(src.mem, &vv)) && vv.isContainer())
                    {
                        stk.push_back(std::move(s)); // resume it later
                        s = std::move(State(vv));
                        goto next;
                    }
                }
                func.EndArray(Wrap(src.mem, s.v));
                break;
            }

            case Var::TYPE_MAP:
            {
                const Iter end = s.v->map_unsafe()->end();
                while (s.it.map != end)
                {
                    PoolStr k = src.mem->getSL(s.it.map.key());
                    func.Key(k.s, k.len);
                    Ref vv = s.it.map.value();
                    ++s.it.map;
                    if (func(Wrap(*src.mem, &vv)) && vv.isContainer())
                    {
                        stk.push_back(std::move(s)); // resume it later
                        s = std::move(State(vv));
                        goto next;
                    }
                }
                func.EndObject(Wrap(*src.mem, s.v));
                break;
            }

            default: assert(false); break; // unreachable
        }
    }
}
