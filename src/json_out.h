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
bool emit(Wr& writer, const Var& v)
{
    bool recurse = false;
    switch(v.type())
    {
        case Var::TYPE_NULL:   writer.Null(); break;
        case Var::TYPE_BOOL:   writer.Bool(!!v.u.ui); break;
        case Var::TYPE_INT:    writer.Int64(v.u.i); break;
        case Var::TYPE_UINT:   writer.Uint64(v.u.i); break;
        case Var::TYPE_FLOAT:  writer.Double(v.u.f); break;
        case Var::TYPE_STRING: writer.String(v.u.s, v._size()); break;

        case Var::TYPE_MAP:
            writer.StartObject();
            recurse = !v.u.m->empty();
            if(!recurse)
                writer.EndObject();
            break;

        case Var::TYPE_ARRAY:
            writer.StartArray();
            recurse = !!v._size();
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
    inline bool operator()(const Var& v)
    {
        return json_out::emit(wr, v);
    }

    inline void EndArray() { wr.EndArray(); }
    inline void EndObject() { wr.EndObject(); }
    inline void Key(const char *k, size_t len) { wr.Key(k, len); }
};

}; // end namespace json_out


template<typename Output>
void writeJson(Output& out, const Var& src, bool pretty)
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
