#include <assert.h>
#include <utility>
#include "viewxform.h"
#include "viewexec.h"
#include "util.h"

namespace view {

// unpack arrays and maps, skip anything else
void transformUnpack(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    newframe.store = std::move(oldframe.store);

    const size_t n = oldframe.refs.size();

    // figure out new size after everything is unpacked
    size_t nn = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const VarEntry& src = oldframe.refs[i];
        if (src.ref.v->isContainer())
            nn += src.ref.size();
    }

    newframe.refs.reserve(nn);

    for (size_t i = 0; i < n; ++i)
    {
        const VarEntry& src = oldframe.refs[i];
        switch (src.ref.type())
        {
        case Var::TYPE_ARRAY:
            for (size_t k = 0; k < nn; ++k)
            {
                VarEntry e{ src.ref.at(k), 0 };
                newframe.refs.push_back(std::move(e));
            }
            break;

        case Var::TYPE_MAP:
        {
            const Var::Map* m = src.ref.v->map_unsafe();
            for (Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                VarEntry e{ VarCRef(src.ref.mem, &it.value()), it.key() };
                newframe.refs.push_back(std::move(e));
            }
        }
        break;

        default: assert(false); break;
        }
    }
}

void transformToInt(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    const size_t n = oldframe.store.size();
    newframe.store.reserve(n);

    for (size_t i = 0; i < n; ++i)
    {
        VarEntry& src = oldframe.refs[i];
        Var newval;
        switch (src.ref.type())
        {
        case Var::TYPE_INT:
        case Var::TYPE_UINT:
            newframe.refs.push_back(std::move(src));
            continue;

        case Var::TYPE_STRING:
        {
            const PoolStr ps = src.ref.asString();
            s64 val;
            if (!strtoi64NN(&val, ps.s, ps.len).ok())
                break; // null val
            newval.setInt(mem, val);
        }
        break;

        default:;
            // null val
        }
        newframe.addAbs(mem, std::move(newval), src.key);
    }

    assert(newframe.refs.size() == oldframe.refs.size());
}

void transformCompact(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    newframe.store = std::move(oldframe.store);
    const size_t n = oldframe.refs.size();
    newframe.refs.reserve(n);

    size_t w = 0;
    for (size_t i = 0; i < n; ++i)
        if (!oldframe.refs[i].ref.isNull())
            oldframe.refs[w++] = oldframe.refs[i];

    oldframe.refs.resize(w);
    newframe.refs = std::move(oldframe.refs);
}

void transformAsArray(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    Var arr;
    const size_t N = oldframe.refs.size();
    Var* a = arr.makeArray(mem, N);
    if (N)
    {
        const Var* mybegin = &oldframe.store.front();
        const Var* myend = &oldframe.store.back();
        for (size_t i = 0; i < N; ++i)
        {
            // We're making an array, so any keys get lost
            VarCRef r = oldframe.refs[i].ref;
            if (r.mem == &mem && mybegin <= r.v && r.v <= myend) // if we own the memory, we can just move the thing
                a[i] = std::move(*const_cast<Var*>(r.v));
            else // but if it's in some other memory space, we must clone it
                a[i] = std::move(r.v->clone(mem, *r.mem));
        }
    }
    newframe.store.reserve(1);
    newframe.addAbs(mem, std::move(arr), 0);
}

void transformAsMap(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    Var mp;
    const size_t N = oldframe.refs.size();
    Var::Map* m = mp.makeMap(mem, N);
    const Var* mybegin = &oldframe.store.front();
    const Var* myend = &oldframe.store.back();
    for (size_t i = 0; i < N; ++i)
    {
        // If the element didn't originally come from a map, drop it.
        // Since we don't know the key to save this under there's nothing we can do
        // TODO: error out instead?
        if (!oldframe.refs[i].key)
            continue;

        VarCRef r = oldframe.refs[i].ref;
        StrRef k = oldframe.refs[i].key;
        if (r.mem == &mem && mybegin <= r.v && r.v < myend) // if we own the memory, we can just move the thing
            m->put(mem, k, std::move(*const_cast<Var*>(r.v)));
        else // but if it's in some other memory space, we must clone it
        {
            PoolStr ps = r.mem->getSL(k);
            assert(ps.s);
            Var& dst = m->putKey(mem, ps.s, ps.len);
            dst = std::move(r.v->clone(mem, *r.mem));
        }
    }
    newframe.store.reserve(1);
    newframe.addAbs(mem, std::move(mp), 0);
}

void transformToKeys(TreeMem& mem, StackFrame& newframe, StackFrame& oldframe)
{
    const size_t N = oldframe.refs.size();
    Var tmp;
    for (size_t i = 0; i < N; ++i)
        if(StrRef s = oldframe.refs[i].key)
        {
            tmp.setStrRef(mem, s);
            newframe.addRel(mem, std::move(tmp), s);
        }
    newframe.makeAbs();
}

} // end namespace view
