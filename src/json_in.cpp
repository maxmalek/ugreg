#include "json_in.h"

#include <sstream>
#include <vector>
#include <utility>
#include <assert.h>

#include "datatree.h"
#include "jsonstreamwrapper.h"

#include "rapidjson/rapidjson.h"
#include "rapidjson/document.h"
#include "rapidjson/reader.h"

// IDEA: https://github.com/simdjson/simdjson for extra speed

// FIXME: make sure this is the set of flags we want
// It's a bit more relaxed since we act as middleware,
// so it's better to accept as much json as possible,
// and not freak out due to an extra comma and such.
static const unsigned ParseFlagsDestructive = 0
    //| rapidjson::kParseInsituFlag
    | rapidjson::kParseValidateEncodingFlag
    | rapidjson::kParseIterativeFlag
    | rapidjson::kParseFullPrecisionFlag
    | rapidjson::kParseCommentsFlag
    | rapidjson::kParseNanAndInfFlag
    | rapidjson::kParseEscapedApostropheFlag
    | rapidjson::kParseTrailingCommasFlag;


// Must not store any own data since this struct is re-created on every parse() call!
struct JsonLoader
{
    struct Frame
    {
        Frame(const Frame&) = delete; // Because Var isn't default-copyable either
        Frame(Frame&& o)
            : v(std::move(o.v)), m(o.m), vals(std::move(o.vals)), lastkey(o.lastkey), _mem(o._mem)
        {
            o.m = 0;
            o.lastkey = 0;
        }
        Frame(TreeMem& mem, bool ismap)
            : v(), m(ismap ? v.makeMap(mem) : 0), lastkey(0), _mem(mem) {}
        ~Frame()
        {
            v.clear(_mem);
            // don't touch m as it's already cleared when v is a map
            if(const size_t n = vals.size())
                for(size_t i = 0; i < n; ++i)
                    vals[i].clear(_mem);
            if(lastkey)
                _mem.freeS(lastkey);
        }
        Var v;
        Var::Map * m; // NULL if array
        std::vector<Var> vals; // only used if array (aka m == NULL)
        StrRef lastkey;
        TreeMem& _mem;
    };


// ---- begin rapidjson stuff -----

    bool Null() { return _emit(Var()); }
    bool Bool(bool b) { return _emit(b); }
    bool Int(int i) { return Int64(i); }
    bool Uint(unsigned u) { return Uint64(u); }
    bool Int64(int64_t i) { return _emit((s64)i); }
    bool Uint64(uint64_t u) { return _emit((u64)u); }
    bool Double(double d) { return _emit(d); }
    bool RawNumber(const char* str, size_t length, bool copy)
    {
        // Note: string is *not* \0-terminated!
        return true; // FIXME: ???
    }
    bool String(const char* str, size_t length, bool copy)
    {
        return _emit(Var(_mem, str, length));
    }
    bool StartObject()
    {
        frames.emplace_back(Frame(_mem, true));
        return true;
    }
    bool Key(const char* str, size_t length, bool copy)
    {
        Frame& f = frames.back();
        assert(!f.lastkey);
        f.lastkey = _mem.putNoRefcount(str, length); // the refcount is increased by emplace() in _emit()
        assert(f.lastkey);
        return true;
    }
    bool EndObject(size_t memberCount)
    {
        // FIXME: make sure this is caught in rapidjson and can't crash here
        assert(frames.size());
        assert(frames.back().m);
        return _emit(_popframe());
    }
    bool StartArray()
    {
        frames.emplace_back(Frame(_mem, false));
        return true;
    }
    bool EndArray(size_t elementCount)
    {
        assert(frames.size());
        assert(!frames.back().m);
        return _emit(_popframe());
    }

// ---- end rapidjson stuff -----

    std::vector<Frame> frames;
    Var root;
    TreeMem& _mem;

    JsonLoader(TreeMem& mem) : _mem(mem) {}
    ~JsonLoader()
    {
        root.clear(_mem); // in case we failed to load and there's leftover crap
    }

    bool parseDestructive(BufferedReadStream& stream);
    bool _emit(Var&& x);
    Var _popframe();
};

bool JsonLoader::_emit(Var&& x)
{
    if(frames.size())
    {
        Frame& f = frames.back();
        if(f.m)
        {
            assert(f.lastkey);
            f.m->emplace(_mem, f.lastkey, std::move(x));
            f.lastkey = 0;
        }
        else
            f.vals.emplace_back(std::move(x));
    }
    else
    {
        assert(root.type() == Var::TYPE_NULL);
        root = std::move(x);
    }
    return true;
}


// TODO: this function is too slow and needs to be optimized.
// Consider re-using frames instead of just deleting them
Var JsonLoader::_popframe()
{
    assert(frames.size());
    Frame& f = frames.back();

    Var tmp;
    if(f.m) // move values over if it's an array
        tmp = std::move(f.v);
    else
    {
        assert(tmp.type() == Var::TYPE_NULL);
        Var *a = tmp.makeArray(_mem, f.vals.size());
        std::move(f.vals.begin(), f.vals.end(), a); // move range
        f.vals.clear();
    }

    frames.pop_back(); // done with this frame; this also clears everything
    return tmp;
}


bool JsonLoader::parseDestructive(BufferedReadStream& stream)
{
    rapidjson::Reader rd;
    return rd.Parse<ParseFlagsDestructive>(stream, *this);
}

bool loadJsonDestructive(VarRef& dst, BufferedReadStream& stream)
{
    stream.init();
    JsonLoader ld(dst.mem);
    if(!ld.parseDestructive(stream))
        return false;

    *dst.v = std::move(ld.root);
    return true;
}
