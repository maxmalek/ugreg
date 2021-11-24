#pragma once

#include <assert.h>

#include <vector>
#include <string>

#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "treeiter.h"
#include "jsonstreamwrapper.h"

namespace json_out { // private namespace

// returns false if object was emitted and is finished, true if it's a container
// and objects inside must be looked at
template<typename Wr>
void emit(Wr& writer, VarCRef v)
{
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
            break;

        case Var::TYPE_ARRAY:
            writer.StartArray();
            break;

        default:
            assert(false);
    }
}

template<typename Wr>
struct WriterFunctor : public ConstTreeIterFunctor
{
    Wr& wr;
    WriterFunctor(Wr& wr) : wr(wr) {}
    inline bool operator()(VarCRef v)
    {
        json_out::emit(wr, v);
        return true;
    }

    inline void EndArray(VarCRef) { wr.EndArray(); }
    inline void EndObject(VarCRef) { wr.EndObject(); }
    inline void Key( const char *k, size_t len) { wr.Key(k, len); }
};

}; // end namespace json_out


template<typename Output>
void writeJson_T(Output& out, const VarCRef src, bool pretty)
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

// Does not attempt to check stream validity. If you need to get out, throw an exception
// in the stream's write function
void writeJson(BufferedWriteStream& out, const VarCRef src, bool pretty);

std::string dumpjson(VarCRef ref, bool pretty = false);
