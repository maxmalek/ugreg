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
    // A frame is pushed onto the frame stack whenever a map or array begins
    struct Frame
    {
        Frame(const Frame&) = delete; // Because Var isn't default-copyable either
        Frame(Frame&& o) noexcept
            : v(std::move(o.v)), m(o.m), vals(std::move(o.vals)), lastkey(o.lastkey), _mem(o._mem)
        {
            o.m = 0;
            o.lastkey = 0;
        }
        Frame(TreeMem& mem)
            : v(), m(0), lastkey(0), _mem(mem) {}
        void makemap(TreeMem& mem) // if this isn't called, it's an array
        {
            assert(v.isNull());
            m = v.makeMap(mem);
        }
        ~Frame()
        {
            clear();
        }
        void clear()
        {
            v.clear(_mem);
            // don't touch m as it's already cleared when v is a map
            if(const size_t n = vals.size())
                for(size_t i = 0; i < n; ++i)
                    vals[i].clear(_mem);
            vals.clear();
            // FIXME: fixup the refcounting here. this causes issues with errorneous json.
            //       for now, this is a harmless leak. the string stays in the pool and will be re-used in future.
            //if(lastkey)
            //    _mem.freeS(lastkey);
            m = 0;
            lastkey = 0;
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
        _pushframe(true);
        return true;
    }
    bool Key(const char* str, size_t length, bool copy)
    {
        Frame& f = _topframe();
        assert(f.m);
        assert(!f.lastkey);
        f.lastkey = _mem.putNoRefcount(str, length); // the refcount is increased by emplace() in _emit()
        assert(f.lastkey);
        return true;
    }
    bool EndObject(size_t memberCount)
    {
        // FIXME: make sure this is caught in rapidjson and can't crash here
        assert(_topframe().m);
        return _emit(_popframe());
    }
    bool StartArray()
    {
        _pushframe(false);
        return true;
    }
    bool EndArray(size_t elementCount)
    {
        assert(!_topframe().m);
        return _emit(_popframe());
    }

// ---- end rapidjson stuff -----

    Var root;
    TreeMem& _mem;

    JsonLoader(TreeMem& mem) : _mem(mem), frameidx(0) {}
    ~JsonLoader()
    {
        root.clear(_mem); // in case we failed to load and there's leftover crap
    }

    template<typename STREAM>
    bool parseDestructive(STREAM& stream)
    {
        rapidjson::Reader rd;
        return rd.Parse<ParseFlagsDestructive>(stream, *this);
    }

    bool _emit(Var&& x);
    Frame& _pushframe(bool ismap);
    Var _popframe();
    Frame& _topframe() { assert(frameidx); return frames[frameidx - 1]; }

private:
    std::vector<Frame> frames;
    size_t frameidx;
};

bool JsonLoader::_emit(Var&& x)
{
    if(frameidx)
    {
        Frame& f = _topframe();
        if(f.m)
        {
            assert(f.lastkey);
            f.m->put(_mem, f.lastkey, std::move(x));
            f.lastkey = 0;
        }
        else
        {
            if(!f.vals.capacity())
                f.vals.reserve(32); // semi-educated guess to reduce allocations for small arrays
            f.vals.push_back(std::move(x));
        }
    }
    else
    {
        assert(root.isNull());
        root = std::move(x);
    }
    return true;
}

JsonLoader::Frame& JsonLoader::_pushframe(bool ismap)
{
    if(frames.size() <= frameidx)
        frames.emplace_back(_mem);

    Frame& f = frames[frameidx++];
    if(ismap)
        f.makemap(_mem);
    return f;
}

Var JsonLoader::_popframe()
{
    assert(frames.size());
    assert(frameidx);
    Frame& f = _topframe();

    Var tmp;
    if(f.m)
        tmp = std::move(f.v);
    else // move values over if it's an array
    {
        Var *a = tmp.makeArray(_mem, f.vals.size());
        std::move(f.vals.begin(), f.vals.end(), a); // move range
        f.vals.clear();
    }

    // Don't actually pop the stack; clear and re-use f later.
    // (This also makes sure that STL containers keep their prev. allocated memory around
    // so we don't keep reallocating new memory over and over)
    f.clear();
    --frameidx;
    return tmp;
}

template<typename STREAM>
static bool _loadJsonDestructive(VarRef dst, STREAM& stream)
{

    JsonLoader ld(*dst.mem);
    if(!ld.parseDestructive(stream))
        return false;

    dst.v->clear(*dst.mem);
    *dst.v = std::move(ld.root);
    return true;
}

bool loadJsonDestructive(VarRef dst, BufferedReadStream& stream)
{
    stream.init();
    return _loadJsonDestructive(dst, stream);
}

bool loadJsonDestructive(VarRef dst, const char* data, size_t len)
{
    rapidjson::MemoryStream ms(data, len);
    return _loadJsonDestructive(dst, ms);
}
