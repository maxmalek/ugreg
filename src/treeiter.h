#pragma once

#include "variant.h"
#include <assert.h>

// Example functor to iterate tree:
/*
struct WriterFunctor
{
    // Var was encountered. Return true to recurse (eventually End*() will be called).
    // Return false not to recurse (End*() will not be called)
    bool operator()(VarCRef v) {}         

    void EndArray() {}       // finished iterating over array
    void EndObject() {}      // finished iterating over map
    void Key(const char *k, size_t len) {} // encountered a map key (op() will be called next)
};
*/

// Pass functor to iterate over all tree nodes.
// Return true from functor to recurse into object/array,
// return false to not recurse.
template<typename Functor>
void treeIter_T(Functor& func, const VarCRef src)
{
    assert(src.v);

    // Early-out if it's a single element
    if (!func(src))
        return;

    // This would be simpler and easier to just write out recursively,
    // but this could then crash due to a stack overflow in pathological cases.
    // So we do the same as rapidjson does when reading:
    // Implement our own stack and instead of recursion, iterate.
    struct State
    {
        State(const Var& x) : v(&x), ty(x.type())
        {
            switch (ty)
            {
            case Var::TYPE_ARRAY: it.array = 0; break;
            case Var::TYPE_MAP: it.map = x.u.m->begin(); break;
            default: assert(false); // State should never get constructed for non-containers
                                    // If this triggers: Did you return true from the functor for a non-container?
            }
        }
        ~State() {}
        const Var* v;
        Var::Type ty; // array or map
        struct // Could be a union but C++11 rules make this more nasty to define than it should be. Lazyness wins.
        {
            size_t array;
            Var::Map::Iterator map;
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
                const Var* const a = s.v->array_unsafe();
                const size_t sz = s.v->_size();
                while (s.it.array < sz)
                {
                    const Var& vv = a[s.it.array];
                    ++s.it.array;
                    if (func(VarCRef(src.mem, &vv)))
                    {
                        stk.emplace_back(s); // resume it later
                        s = std::move(State(vv));
                        goto next;
                    }
                }
                func.EndArray();
                break;
            }

            case Var::TYPE_MAP:
            {
                const Var::Map* m = s.v->map_unsafe();
                Var::Map::Iterator end = m->end();
                while (s.it.map != end)
                {
                    PoolStr k = src.mem.getSL(s.it.map->first);
                    func.Key(k.s, k.len);
                    const Var& vv = s.it.map->second;
                    ++s.it.map;
                    if (func(VarCRef(src.mem, &vv)))
                    {
                        stk.emplace_back(s); // resume it later
                        s = std::move(State(vv));
                        goto next;
                    }
                }
                func.EndObject();
                break;
            }
        }
    }
}
