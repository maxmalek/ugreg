#include "bj.h"
#include "jsonstreamwrapper.h"
#include <vector>
#include <deque>
#include <assert.h>
#include <math.h>
#include "treemem.h"

namespace bj {

static const union {
    int dummy;
    char little;  /* true iff machine is little endian */
} nativeendian = { 1 };

// must fit in 3 bits
enum Op
{
    OP_VALUE,
    OP_INT_POS,
    OP_INT_NEG,
    OP_STRING,
    OP_ARRAY,
    OP_MAP,
    OP_COPY_PREV,
    OP_COPY_CONST,
};

struct Window
{
    Window() : w(0), mask(0) {}

    void init(u8 wbits)
    {
        mask = size_t(1) << wbits;
        v.clear();
        v.resize(size_t(1) << wbits, NULL);
    }

    void emit(const Var *p) { v[w++ & mask] = p; }

    const Var *getOffs(size_t offset) const
    {
        assert(offset);
        return v[(w - offset) & mask];
    }
    size_t find(const Var *x) const // 0 when not found
    {
        size_t n = w < v.size() ? w : v.size();
        for(size_t i = 1; i < n; ++i)
            if(getOffs(i)->compareExactSameMem(*x))
                return i;
        return 0;
    }
    size_t w;
    size_t mask;
    std::vector<const Var*> v;
};

struct ReadState
{
    ReadState(BufferedReadStream& src, TreeMem& mem) : src(src), mem(mem) {}

    bool init()
    {
        if(src.done())
            return false;
        u8 wbits = src.Take();
        win.init(wbits);
        return true;
    }

    BufferedReadStream& src;
    TreeMem& mem;

    //------------------------
    std::vector<const Var*> constants;
    Window win;

    // temporary storage for keys and constants
    // important that this never moves elements because
    // we're keeping pointers to elements in the window!
    // a std::vector is unsuitable, but a std::deque doesn't reallocate so it's fine
    typedef std::deque<Var> TmpStorage;
    TmpStorage tmp;

    ~ReadState()
    {
        for(TmpStorage::iterator it = tmp.begin(); it != tmp.end(); ++it)
            it->clear(mem);
    }
};

inline static size_t readnum(ReadState& rd)
{
    size_t ret = 0;
    u8 c;
    do
    {
        ret <<= 7;
        c = rd.src.Take();
        ret |= c & 0x7f;
    }
    while((c & 0x80) && !rd.src.done());
    return ret;
}

inline static size_t smallnum5(ReadState& rd, u8 a)
{
    return a < 0b11111 ? a : readnum(rd) + a;
}

template<typename T>
static bool readLE(T& dst, ReadState& rd)
{
    union
    {
        u8 b[sizeof(T)];
        T x;
    } u;

    if(nativeendian.little)
    {
        for(size_t i = 0; i < sizeof(T); ++i)
        {
            if(rd.src.done())
                return false;
            u.b[i] = rd.src.Take();
        }
    }
    else
    {
        for (size_t i = sizeof(T); i --> 0; )
        {
            if (rd.src.done())
                return false;
            u.b[i] = rd.src.Take();
        }
    }

    dst = u.x;
    return true;
}

inline static bool readval(Var& dst, ReadState& rd)
{
start:
    u8 a = rd.src.Take();
    const u8 op = a >> 5;
    a &= 0b11111;
    switch(op)
    {
        case OP_VALUE:
            if(!a) // None
            {
                dst.clear(rd.mem);
                rd.win.emit(&dst);
                return true;
            }
            if((a & 0b11110) == 0b00010) // Bool
            {
                dst.setBool(rd.mem, a & 1);
                rd.win.emit(&dst);
                return true;
            }
            if((a & 0b11100) == 0b00100) // Float
            {
                double f = 0;
                switch(a & 0b11)
                {
                    case 0: // Float32
                    {
                        float ff;
                        if(readLE(ff, rd))
                            f = ff;
                        else
                            return false;
                    }
                    break;
                    case 1: // Double
                        if (!readLE(f, rd))
                            return false;
                    break;
                    case 2: // Read +int, cast to f
                        f = (double)readnum(rd);
                        break;
                    case 3: // Read -int, cast to f
                        f = -(double)readnum(rd);
                        break;
                }
                dst.setFloat(rd.mem, f);
                rd.win.emit(&dst);
                return true;
            }
            if(a == 0b01000) // Define constants
            {
                size_t i = readnum(rd);
                const size_t N = readnum(rd);
                const size_t end = i + N;
                if(rd.constants.size() < end)
                    rd.constants.resize(end);
                for( ; i < end; ++i)
                {
                    Var& k = rd.tmp.emplace_back();
                    if(!readval(k, rd))
                        return false;
                    rd.constants[i] = &k;
                }
                goto start;
            }

            assert(false);
            return false;

        case OP_INT_POS:
            dst.setUint(rd.mem, smallnum5(rd, a));
            rd.win.emit(&dst);
            return true;
        case OP_INT_NEG:
            dst.setInt(rd.mem, -s64(smallnum5(rd, a)));
            rd.win.emit(&dst);
            return true;
        case OP_STRING:
        {
            const size_t len = smallnum5(rd, a);
            std::string s;
            s.resize(len);
            for(size_t i = 0; i < len; ++i)
                s[i] = rd.src.Take();
            dst.setStr(rd.mem, s.c_str(), len);
            rd.win.emit(&dst);
            return true;
        }
        case OP_ARRAY:
        {
            const size_t len = smallnum5(rd, a);
            Var * const arr = dst.makeArray(rd.mem, len);
            for(size_t i = 0; i < len; ++i)
                if(!readval(arr[i], rd))
                    return false;
            rd.win.emit(&dst);
            return true;
        }
        case OP_MAP:
        {
            const size_t len = smallnum5(rd, a);
            Var::Map * const m  = dst.makeMap(rd.mem, len);
            bool ok = true;
            for(size_t i = 0; i < len && ok; ++i)
            {
                Var& k = rd.tmp.emplace_back();
                ok = readval(k, rd)
                    && (k.type() == Var::TYPE_STRING)
                    && readval(m->getOrCreate(rd.mem, k.asStrRef()), rd);
            }
            rd.win.emit(&dst);
            return ok;
        }
        case OP_COPY_PREV:
        {
            if(const Var *prev = rd.win.getOffs(smallnum5(rd, a) + 1))
            {
                dst = std::move(prev->clone(rd.mem, rd.mem));
                rd.win.emit(prev);
                return true;
            }
            return false;
        }
        case OP_COPY_CONST:
        {
            const size_t idx = smallnum5(rd, a);
            if(idx < rd.constants.size())
            {
                if(const Var *c = rd.constants[idx])
                {
                    dst = std::move(c->clone(rd.mem, rd.mem));
                    rd.win.emit(c);
                    return true;
                }
            }
            return false;
        }
    }

    assert(false);
    return false;
}

bool decode_json(VarRef dst, BufferedReadStream& src)
{
    ReadState rd(src, *dst.mem);
    return rd.init() && readval(*dst.v, rd);
}

// -----------------------------------------------------------


struct WriteState
{
    WriteState(BufferedWriteStream& dst, const TreeMem& mem)
        : dst(dst), mem(mem), strcoll(mem.collate())
    {}

    BufferedWriteStream& dst;
    const TreeMem& mem;

    //------------------------
    std::vector<const Var*> constants;
    Window win;
    const StringPool::StrColl strcoll;
};

struct IntEncoder
{
    u8 *begin;
    size_t n;
    u8 encode(u64 x, u8 rem)
    {
        n = 0;
        begin = &buf[16];
        if(x < rem)
            return u8(x);

        x -= rem;
        
        // encode backwards so that when read forwards it's big endian
        while(x > 0x7f)
        {
            *--begin = 0x80 | (x & 0x7f); // encode backwards
            x >>= 7;
            ++n;
        }
        *--begin = u8(x); // highest bit not set
        ++n;

        return rem;
    };
private:
    u8 buf[16];
};

template<typename T>
static size_t writeLE(WriteState& wr, T val)
{
    union
    {
        u8 b[sizeof(T)];
        T x;
    } u;
    u.x = val;

    if (nativeendian.little)
        wr.dst.Write((const char*)&u.b[0], sizeof(T));
    else
        for (size_t i = sizeof(T); i-- > 0; )
            wr.dst.Put(u.b[i]);

    return sizeof(T);
}


static size_t putOpAndSize(WriteState& wr, Op op, u64 size)
{
    IntEncoder enc;
    u8 rem = enc.encode(size, 0b11111);
    wr.dst.Put(op | (rem << 3));
    for(size_t i = 0; i < enc.n; ++i)
        wr.dst.Put(enc.begin[i]);
    return enc.n + 1;
}

static size_t putSize(WriteState& wr, u64 size)
{
    IntEncoder enc;
    enc.encode(size, 0);
    for (size_t i = 0; i < enc.n; ++i)
        wr.dst.Put(enc.begin[i]);
    return enc.n;
}

static size_t putStr(WriteState& wr, StrRef ref, size_t len)
{
    const char *s = wr.mem.getS(ref);
    size_t n = putOpAndSize(wr, OP_STRING, len);
    wr.dst.Write(s, len);
    return len + n;
}

static size_t putStr(WriteState& wr, StrRef ref)
{
    PoolStr ps = wr.mem.getSL(ref);
    size_t n = putOpAndSize(wr, OP_STRING, ps.len);
    wr.dst.Write(ps.s, ps.len);
    return ps.len + n;
}

static size_t encodeVal(WriteState& wr, const Var& in)
{
    switch(in.type())
    {
        case Var::TYPE_NULL:
            wr.dst.Put(OP_VALUE | 0);
            return 1;

        case Var::TYPE_BOOL:
            wr.dst.Put(OP_VALUE | (0b00010 << 3) | u8(!!in.u.ui));
            return 1;

        case Var::TYPE_INT:
            if(in.u.i < 0)
                return putOpAndSize(wr, OP_INT_NEG, -in.u.i);
        [[fallthrough]];

        case Var::TYPE_UINT:
            return putOpAndSize(wr, OP_INT_POS, in.u.ui);

        case Var::TYPE_FLOAT:
        {
            double f = in.u.f;
            if(trunc(f) == f && abs(f) < 0x7fffffff) // can be sanely encoded as int?
            {
                u8 bits = 0b00100;
                if(f < 0)
                {
                    bits = 0b00111;
                    f = -f;
                }
                wr.dst.Put(OP_VALUE | (bits << 3));
                return putSize(wr, u64(f)) + 1;
            }
            float ff = (float)f;
            if(f == (double)ff) // fits losslessly in float?
            {
                wr.dst.Put(OP_VALUE | (0b00100 << 3));
                return writeLE(wr, ff) + 1;
            }

            // write as double
            wr.dst.Put(OP_VALUE | (0b00101 << 3));
            return writeLE(wr, f) + 1;
        }

        case Var::TYPE_STRING:
            return putStr(wr, in.asStrRef(), in._size());

        case Var::TYPE_ARRAY:
        {
            const size_t N = in._size();
            size_t sz = putOpAndSize(wr, OP_ARRAY, N);
            const Var *a = in.array_unsafe();
            for(size_t i = 0; i < N; ++i)
            {
                const size_t vsz = encodeVal(wr, a[i]);
                if(!vsz)
                    return 0;
                sz += vsz;
            }
            return sz;
        }

        case Var::TYPE_MAP:
        {
            const Var::Map *m = in.map_unsafe();
            const size_t N = m->size();
            size_t sz = putOpAndSize(wr, OP_MAP, N);
            for(Var::Map::Iterator it = m->begin(); it != m->end(); ++it)
            {
                sz += putStr(wr, it.key());
                size_t vsz = encodeVal(wr, it.value());
                if(!vsz)
                    return 0;
                sz += vsz;
            }
            return sz;
        }
    }

    assert(false);
    return 0;
}

size_t encode(BufferedWriteStream& dst, const VarCRef& json, u8 windowSizeLog)
{
    WriteState wr(dst, *json.mem);
    wr.win.init(windowSizeLog);
    dst.Put(windowSizeLog);
    return encodeVal(wr, *json.v) + 1;
}



} // end namespace bj
