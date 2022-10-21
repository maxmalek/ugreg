#include "bj.h"

#include <assert.h>
#include <math.h>
#include <algorithm>

#include "treemem.h"
#include "jsonstreamwrapper.h"
#include "util.h"

namespace bj {

static const size_t MaxSizeBits = sizeof(u64) * CHAR_BIT;


template<typename T>
static T _fail(T val, const char *msg)
{
    puts(msg);
    return val;
}

#define FAIL(ret, msg) _fail(ret, msg)

static const union {
    int dummy;
    char little;  /* true iff machine is little endian */
} nativeendian = { 1 };

// must fit in 3 bits
enum Op : u8
{
    OP_VALUE,
    OP_INT_POS,
    OP_INT_NEG,
    OP_STRING,
    OP_ARRAY,
    OP_MAP,
    OP_COPY_CONST,
};

inline static u8 encodeOp(Op op, u8 bits)
{
    return (op << 5) | bits;
}

struct ReadState
{
    ReadState(BufferedReadStream& src, TreeMem& mem, const Limits& lim) : src(src), mem(mem) {}

    BufferedReadStream& src;
    TreeMem& mem;
    const Limits lim;

    //------------------------
    Var constantsHolder;
    Var *constants = NULL;
    size_t constantsMax = 0;
    bool definingConstants = false;
    std::string tmpstr;

    ~ReadState()
    {
        constantsHolder.clear(mem);
    }
};

inline static bool readnum(u64& dst, ReadState& rd)
{
    dst = 0;
    u64 sh = 0;
    u8 c;
    do
    {
        if(rd.src.done())
            return FAIL(false, "stream end");
        if(sh >= MaxSizeBits)
            return FAIL(false, "too many bits");
        c = rd.src.Take();
        const size_t add = u64(c & 0x7f) << sh;
        if(add_check_overflow(&dst, dst, add))
            return FAIL(false, "overflow");
        sh += 7;
    }
    while((c & 0x80) && !rd.src.done());

    return true;
}

inline static bool smallnum5(u64& dst, ReadState& rd, u8 a)
{
    if(a < 0b11111)
    {
        dst = a;
        return true;
    }

    return readnum(dst, rd) && !add_check_overflow<u64>(&dst, dst, a);
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
                return FAIL(false, "stream end");
            u.b[i] = rd.src.Take();
        }
    }
    else
    {
        for (size_t i = sizeof(T); i --> 0; )
        {
            if (rd.src.done())
                return FAIL(false, "stream end");
            u.b[i] = rd.src.Take();
        }
    }

    dst = u.x;
    return true;
}

inline static bool readval(Var& dst, ReadState& rd)
{
start:
    if(rd.src.done())
        return FAIL(false, "end of stream");
    u8 a = rd.src.Take();
    const Op op = Op(a >> 5);
    a &= 0b11111;
    switch(op)
    {
        case OP_VALUE:
            if(!a) // None
            {
                dst.clear(rd.mem);
                return true;
            }
            if((a & 0b11110) == 0b00010) // Bool
            {
                dst.setBool(rd.mem, a & 1);
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
                            return FAIL(false, "read float32");
                    }
                    break;
                    case 1: // Double
                        if (!readLE(f, rd))
                            return FAIL(false, "read double");
                    break;
                    case 2: // Read +int, cast to f
                    {
                        u64 tmp;
                        if(!readnum(tmp, rd))
                            return FAIL(false, "read +int -> f");
                        f = (double)tmp;
                        break;
                    }
                    case 3: // Read -int, cast to f
                        u64 tmp;
                        if(!readnum(tmp, rd))
                            return FAIL(false, "read -int -> f");
                        f = -(double)tmp;
                        break;
                }
                dst.setFloat(rd.mem, f);
                return true;
            }
            if(a == 0b01000) // Define constants
            {
                if(rd.definingConstants)
                    return FAIL(false, "can't nest def constants");
                rd.definingConstants = true;
                u64 i, n;
                if(!readnum(i, rd))
                    return FAIL(false, "def constants begin idx");
                if(!readnum(n, rd))
                    return FAIL(false, "def constants size");
                u64 end;
                if(add_check_overflow(&end, i, n))
                    return FAIL(false, "def constants size overflow");
                if(end)
                {
                    if(end > rd.lim.constants)
                        return FAIL(false, "def constants table exceeds limits");
                    if(i > rd.constantsMax)
                        return FAIL(false, "def constants non-contiguous");
                    Var tmp;
                    const size_t oldsize = rd.constantsHolder._size();
                    assert(oldsize == rd.constantsMax);
                    // we know that n elems follow. so we don't have to init array elems only to overwrite them later
                    // it's possible that malicious inputs request a large block of memory,
                    // in that case we might fail later when trying to read elements.
                    if(oldsize < end)
                    {
                        // precond: arr[0..oldsize) is inited
                        Var * const arr = rd.constantsHolder.makeArrayUninitialized_Dangerous(rd.mem, end);
                        if(!arr)
                        {
                            i = oldsize;
                            goto failclean;
                        }
                        rd.constants = arr;
                    }
                    for( ; i < oldsize; ++i) // this was previously initialized. overwrite elems cleanly.
                    {
                        Var& k = rd.constants[i];
                        if(!readval(k, rd))
                            return FAIL(false, "def constants elems");
                    }
                    // new, uninited memory. must move-construct with placement new
                    for( ; i < end; ++i)
                    {
                        if (!readval(tmp, rd))
                        {
                            tmp.clear(rd.mem);
                            // ouch. array is partially uninited. clear valid part, then drop it
failclean:
                            Var::ClearArray(rd.mem, rd.constants, i);
                            rd.constants = rd.constantsHolder.makeArrayUninitialized_Dangerous(rd.mem, 0);
                            rd.constantsHolder.clear(rd.mem); // to be safe: do this here so that we won't crash later in the dtor in case anything is wrong
                            return FAIL(false, "def constants table (new elems)");
                        }
                        new (&rd.constants[i]) Var(std::move(tmp));
                        // tmp now empty and ready to be filled again
                        assert(tmp.isNull());
                    }
                }
                if(end > rd.constantsMax)
                    rd.constantsMax = end;
                rd.definingConstants = false;
                goto start;
            }

            return FAIL(false, "OP_VALUE unknown bits");

        case OP_INT_POS:
        {
            u64 n;
            if(!smallnum5(n, rd, a))
                return FAIL(false, "OP_INT_POS decode");
            dst.setUint(rd.mem, n);
            return true;
        }
        case OP_INT_NEG:
        {
            u64 n;
            if(!smallnum5(n, rd, a))
                return FAIL(false, "OP_INT_NEG decode");
            if((s64(n) < 0) == (-s64(n) < 0))
                return FAIL(false, "OP_INT_NEG consistency");
            s64 neg = -s64(n);
            if(neg >= 0)
                return FAIL(false, "OP_INT_NEG still negative");
            dst.setInt(rd.mem, neg);
            return true;
        }
        case OP_STRING:
        {
            u64 len;
            if(!smallnum5(len, rd, a))
                return FAIL(false, "OP_STRING length");
            if(len > rd.lim.maxsize)
                return FAIL(false, "string size exceeds limit");
            if(rd.src.availBuffered() >= len) // copy directly
            {
                dst.setStr(rd.mem, (const char*)rd.src.ptr(), len);
                rd.src.advanceBuffered(len);
            }
            else // slow path
            {
                std::string& s = rd.tmpstr; // is a member to avoid repeated (re-)allocation
                s.resize(len);
                for(size_t i = 0; i < len; ++i)
                {
                    if(rd.src.done())
                        return FAIL(false, "stream end while reading string");
                    s[i] = rd.src.Take();
                }
                dst.setStr(rd.mem, s.c_str(), len);
            }
            return true;
        }
        case OP_ARRAY:
        {
            u64 len;
            if(!smallnum5(len, rd, a))
                return FAIL(false, "OP_ARRAY size");
            if(len > rd.lim.maxsize)
                return FAIL(false, "OP_ARRAY size exceeds limit");
            Var * const arr = dst.makeArrayUninitialized_Dangerous(rd.mem, len);
            if(len && !arr)
                return FAIL(false, "OP_ARRAY alloc");
            Var tmp;
            for(size_t i = 0; i < len; ++i)
                if(readval(tmp, rd))
                {
                    new (&arr[i]) Var(std::move(tmp));
                    assert(tmp.isNull());
                }
                else
                {
                    tmp.clear(rd.mem);
                    // ouch. array is partially uninited. clear valid part, then drop array
                    Var::ClearArray(rd.mem, arr, i);
                    dst.makeArrayUninitialized_Dangerous(rd.mem, 0);
                    dst.clear(rd.mem);
                    return FAIL(false, "OP_ARRAY value");
                }
            return true;
        }
        case OP_MAP:
        {
            u64 len;
            if(!smallnum5(len, rd, a))
                return FAIL(false, "OP_MAP size");
            if(len > rd.lim.maxsize)
                return FAIL(false, "OP_MAP size exceeds limit");
            Var::Map * const m  = dst.makeMap(rd.mem, len);
            if(!m)
                return FAIL(false, "OP_MAP alloc");
            Var k;
            for(size_t i = 0; i < len; ++i)
            {
                if(!(readval(k, rd)
                    && k.type() == Var::TYPE_STRING
                    && readval(m->getOrCreate(rd.mem, k.asStrRef()), rd)
                ))
                {
                    k.clear(rd.mem);
                    return FAIL(false, "OP_MAP key/value pair");
                }
            }
            k.clear(rd.mem);
            return true;
        }
        case OP_COPY_CONST:
        {
            size_t idx;
            if(!smallnum5(idx, rd, a))
                return FAIL(false, "OP_COPY_CONST read index");
            if(idx < rd.constantsMax)
            {
                const Var& c = rd.constants[idx];
                dst.clear(rd.mem);
                dst = std::move(c.clone(rd.mem, rd.mem));
                return true;
            }
            return FAIL(false, "OP_COPY_CONST index out of bounds");
        }
    }

    return FAIL(false, "unhandled OP");
}

bool decode_json(VarRef dst, BufferedReadStream& src, const Limits& lim)
{
    ReadState rd(src, *dst.mem, lim);
    return readval(*dst.v, rd);
}

// -----------------------------------------------------------


struct WriteState
{
    static bool _RemoveIf(const StringPool::StrAndCount& a)
    {
        return a.count < 2;
    }
    static bool _Order(const StringPool::StrAndCount& a, const StringPool::StrAndCount& b)
    {
        return a.count > b.count // highest count comes first
            || (a.count == b.count && a.s < b.s);
    }
    WriteState(BufferedWriteStream& dst, const TreeMem& mem, BlockAllocator *alloc)
        : dst(dst), mem(mem), balloc(alloc)
    {}

    ~WriteState()
    {
        if(balloc)
            ref2idx.dealloc(*balloc);
    }

    size_t poolAndEmitStrings();

    BufferedWriteStream& dst;
    const TreeMem& mem;
    BlockAllocator * const balloc;

    //------------------------
    std::vector<const Var*> constants;

    typedef TinyHashMap<size_t> Ref2Idx;
    Ref2Idx ref2idx;
};

// rem is part of another byte written before the rest of the encoded number,
// -> can't just write to the output stream; need to buffer the result
struct IntEncoder
{
    size_t n;
    u8 encode(u64 x, u8 rem)
    {
        if(x < rem)
        {
            n = 0;
            return u8(x);
        }

        x -= rem;

        size_t k = 1;
        u8 *p = &buf[0];
        while(x > 0x7f)
        {
            *p++ = 0x80 | (x & 0x7f); // highest bit set -> continue
            x >>= 7;
            ++k;
        }
        *p++ = u8(x); // highest bit not set -> end
        this->n = k;

        return rem;
    };
    u8 buf[16]; // large enough to encode any u64
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
    wr.dst.Put(encodeOp(op, rem));
    for(size_t i = 0; i < enc.n; ++i)
        wr.dst.Put(enc.buf[i]);
    return enc.n + 1;
}

static size_t putSize(WriteState& wr, u64 size)
{
    IntEncoder enc;
    enc.encode(size, 0);
    for (size_t i = 0; i < enc.n; ++i)
        wr.dst.Put(enc.buf[i]);
    return enc.n;
}

// always emit string (no constant table lookup)
static size_t putStrRaw(WriteState& wr, const char *s, size_t len)
{
    size_t n = putOpAndSize(wr, OP_STRING, len);
    wr.dst.Write(s, len);
    return len + n;
}

static size_t putStrRaw(WriteState& wr, StrRef ref, size_t len)
{
    const char *s = wr.mem.getS(ref);
    return putStrRaw(wr, s, len);
}

static size_t putStrRaw(WriteState& wr, StrRef ref)
{
    PoolStr ps = wr.mem.getSL(ref);
    return putStrRaw(wr, ps.s, ps.len);
}

// emit lookup to constant table if the string is there,
// emit string otherwise
static size_t putStr(WriteState& wr, StrRef ref, size_t len)
{
    size_t *p = wr.ref2idx.getp(ref);
    return p
        ? putOpAndSize(wr, OP_COPY_CONST, *p)
        : putStrRaw(wr, ref, len);
}

static size_t putStr(WriteState& wr, StrRef ref)
{
    size_t sz = wr.mem.getL(ref);
    return putStr(wr, ref, sz);
}

size_t WriteState::poolAndEmitStrings()
{
    if(!balloc)
        return 0;

    StringPool::StrColl strcoll = mem.collate();

    // remove strings that are not worth pooling
    strcoll.erase(std::remove_if(strcoll.begin(), strcoll.end(), _RemoveIf), strcoll.end());

    ref2idx.reserve(*balloc, strcoll.size());

    size_t ret = 0;
    if(size_t N = strcoll.size())
    {
        std::sort(strcoll.begin(), strcoll.end(), _Order); // most common strings first

        dst.Put(encodeOp(OP_VALUE, 0b01000));
        ret++;
        ret += putSize(*this, 0);
        ret += putSize(*this, N);
        for(size_t i = 0; i < N; ++i)
        {
            ref2idx.at(*balloc, strcoll[i].ref) = i;
            ret += putStrRaw(*this, strcoll[i].s.c_str(), strcoll[i].s.length());
        }
    }
    return ret;
}

static size_t encodeVal(WriteState& wr, const Var& in)
{
    switch(in.type())
    {
        case Var::TYPE_NULL:
            wr.dst.Put(encodeOp(OP_VALUE, 0));
            return 1;

        case Var::TYPE_BOOL:
            wr.dst.Put(encodeOp(OP_VALUE, 0b00010 | u8(!!in.u.ui)));
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
                u8 bits = 0b00110;
                if(f < 0)
                {
                    bits = 0b00111;
                    f = -f;
                }
                wr.dst.Put(encodeOp(OP_VALUE, bits));
                return putSize(wr, u64(f)) + 1;
            }
            float ff = (float)f;
            if(f == (double)ff) // fits losslessly in float?
            {
                wr.dst.Put(encodeOp(OP_VALUE, 0b00100));
                return writeLE(wr, ff) + 1;
            }

            // write as double
            wr.dst.Put(encodeOp(OP_VALUE, 0b00101));
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
                    return FAIL(0, "encode array value");
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
                    return FAIL(0, "encode map value");
                sz += vsz;
            }
            return sz;
        }

        default: ;
    }

    assert(false);
    return FAIL(0, "encode unhandled type");
}

size_t encode(BufferedWriteStream& dst, const VarCRef& json, BlockAllocator *tmpalloc)
{
    WriteState wr(dst, *json.mem, tmpalloc);
    size_t strsize = wr.poolAndEmitStrings();
    return encodeVal(wr, *json.v) + strsize + 1;
}



} // end namespace bj
