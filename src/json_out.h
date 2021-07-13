#pragma once

#include <assert.h>

#include <vector>

#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "treeiter.h"

namespace json_out { // private namespace

// returns false if object was emitted and is finished, true if it's a container
// and objects inside must be looked at
template<typename Wr>
bool emit(Wr& writer, VarCRef v)
{
    bool recurse = false;
    const Var& vv = *v.v;
    switch(v.type())
    {
        case Var::TYPE_NULL:   writer.Null(); break;
        case Var::TYPE_BOOL:   writer.Bool(!!vv.u.ui); break;
        case Var::TYPE_INT:    writer.Int64(vv.u.i); break;
        case Var::TYPE_UINT:   writer.Uint64(vv.u.ui); break;
        case Var::TYPE_FLOAT:  writer.Double(vv.u.f); break;
        case Var::TYPE_STRING:
        {
            PoolStr ps = v.asString();
            writer.String(ps.s, ps.len);
            break;
        }

        case Var::TYPE_MAP:
            writer.StartObject();
            recurse = !vv.u.m->empty();
            if(!recurse)
                writer.EndObject();
            break;

        case Var::TYPE_ARRAY:
            writer.StartArray();
            recurse = !!vv._size();
            if(!recurse)
                writer.EndArray();
            break;

        default:
            assert(false);
    }
    return recurse;
}

template<typename Wr>
struct WriterFunctor
{
    Wr& wr;
    WriterFunctor(Wr& wr) : wr(wr) {}
    inline bool operator()(VarCRef v)
    {
        return json_out::emit(wr, v);
    }

    inline void EndArray() { wr.EndArray(); }
    inline void EndObject() { wr.EndObject(); }
    inline void Key(const char *k, size_t len) { wr.Key(k, len); }
};

}; // end namespace json_out


template<typename Output>
void writeJson(Output& out, const VarCRef src, bool pretty)
{
    if(pretty)
    {
        rapidjson::PrettyWriter<Output> writer(out);
        json_out::WriterFunctor<decltype(writer)> f(writer);
        treeIter_T(f, src);
    }
    else
    {
        rapidjson::Writer<Output> writer(out);
        json_out::WriterFunctor<decltype(writer)> f(writer);
        treeIter_T(f, src);
    }
}
