#include <string.h>
#include "variant.h"
#include "bj.h"
#include "jsonstreamwrapper.h"
#include "json_in.h"
#include "json_out.h"
#include "scopetimer.h"


struct BufferTestWriteStream : public BufferedWriteStream
{
    BufferTestWriteStream(char *out, size_t sz)
        : BufferedWriteStream(NULL, _Write, tmp, sizeof(tmp))
        , _dst(out), _dstsz(sz), written(0)
    {
    }

    static size_t _Write(const void* src, size_t bytes, BufferedWriteStream* self)
    {
        BufferTestWriteStream* me = static_cast<BufferTestWriteStream*>(self);
        const size_t remain = me->_dstsz - me->written;
        const size_t wr = std::min(bytes, remain);
        memcpy(me->_dst + me->written, src, wr);
        return wr;
    }

    char *_dst;
    const size_t _dstsz;
    size_t written;
    char tmp[256];
};

static bool testsame(VarCRef v, TreeMem& mem)
{
    char buf[1024];
    size_t sz;
    {
        BufferTestWriteStream sm(&buf[0], sizeof(buf));
        sm.init();
        sz = bj::encode(sm, v, &mem);
        assert(sz <= sizeof(buf));
    }
    if(!sz)
        return false;

    Var cp;
    VarRef dst(mem, &cp);
    {
        InplaceStringStream in(buf, sz);
        in.init();
        if(!bj::decode_json(dst, in))
            return false;
    }

    bool ok = v.v->compareExact(*v.mem, cp, mem);
    dst.clear();
    return ok;
}

template<typename T>
bool testval(const T& v, DataTree& tre)
{
    tre.root() = v;
    return testsame(tre.root(), tre);
}

static bool testints()
{
    DataTree tre;

    for(u64 i = 0; i < 0xffff; ++i)
        if(!testval(i, tre))
            return false;

    // signed but positive ints will be decoded as unsigned, which causes exact comparison to fail
    for(s64 i = -0xffff; i < 0; i += 17)
        if(!testval(i, tre))
            return false;

    puts("All ints ok!");
    return true;
}

static void testload(DataTree& tree)
{
char json[] = R""(
{ "people": [
    { "name": "John (r1)", "room": 1 },
    { "name": "Jack (r1)", "room": 1 },
    { "name": "Alyx (r1)", "room": 1 },
    { "name": "Pete (r2)", "room": 2 },
    { "name": "Zuck (r3)", "room": 3 },
    { "name": "Jill (r3)", "room": 3 },
],
    "rooms": [
    { "id": 1, "name": "Room #1", "open": true },
    { "id": 2, "name": "Room #2", "open": false },
    { "id": 3, "name": "Room #3", "open": true },
],
    "test": {
        "a": { "x": 1 },
        "b": { "y": 2 },
        "c": { "z": 3, "x": -1 },
    }
}
)"";

    InplaceStringStream in(&json[0], sizeof(json));
    bool ok = loadJsonDestructive(tree.root(), in);
    assert(ok);
}

int main(int argc, char **argv)
{
    //testints();

    DataTree tre;
    char buf[8*1024];
    FILE *fh;


    fh = fopen("citylots.json", "rb");
    if(!fh)
        abort();

    {
        ScopeTimer t;
        BufferedFILEReadStream json(fh, buf, sizeof(buf));
        json.init();
        loadJsonDestructive(tre.root(), json);
        printf("Loaded JSON in %llu ms\n", t.ms());
    }
    fclose(fh);

    fh = fopen("test.bj", "wb");
    if(!fh)
        abort();

    {
        ScopeTimer t;
        BufferedFILEWriteStream wr(fh, buf, sizeof(buf));
        wr.init();
        size_t sz = bj::encode(wr, tre.root(), &tre);
        printf("Wrote BJ in %llu ms, size = %zu\n", t.ms(), sz);
    }
    fclose(fh);
    tre.root().clear();


    fh = fopen("test.bj", "rb");
    if(!fh)
        abort();

    {
        ScopeTimer t;
        BufferedFILEReadStream rd(fh, buf, sizeof(buf));
        rd.init();
        bool ok = bj::decode_json(tre.root(), rd);
        printf("Loaded BJ in %llu ms, ok = %u\n", t.ms(), ok);
    }
    fclose(fh);
    //puts(dumpjson(tre.root(), true).c_str());


    return 0;
}
