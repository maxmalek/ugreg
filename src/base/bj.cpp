/* Inspired by BSON, BJSON, <insert whatever binary encoding for json here>.
   It's a draft but should be quite a bit more compact than all of the above.
   This code is possibly security-critical since it handles untrusted data.
   It has been AFL-fuzzed to death so beware nontrivial changes! */

#include "bj.h"

#include <assert.h>
#include <math.h>
#include <string.h>
#include <algorithm>

#include "treemem.h"
#include "jsonstreamwrapper.h"
#include "util.h"

namespace bj {

/*
struct Reader
{
    bool Null() { return _emit(Var()); }
    bool Bool(bool b) { return _emit(b); }
    bool Int(int64_t i) { return _emit((s64)i); }
    bool Uint(uint64_t u) { return _emit((u64)u); }
    bool Double(double d) { return _emit(d); }

    // known size containers
    bool BeginArray(size_t size) {}
    bool EndArray(size_t size) {}

    bool BeginMap(size_t size) {}
    bool EndMap(size_t size) {}

    bool BeginConstants(size_t start, size_t n) {}
    bool EndConstants(size_t start, size_t n) {}
    bool EmitConstant(size_t idx) {}
};
*/

struct BjEmit
{
    enum FrameType
    {
        FRM_ARRAY,
        FRM_MAP,
        FRM_CTAB,
        FRM_SENTINEL
    };
    struct Frame
    {
        FrameType ty;
        union
        {
            Var *a; // if array
            Var::Map *m; // if map
        } u;
        size_t idx = 0;
        size_t size = 0;
        Var currentKey; // holds key when filling maps
    };

    BjEmit(TreeMem& mem)
        : mem(mem)
    {
        currentFrame.ty = FRM_SENTINEL;
        currentFrame.u.a = NULL;
        currentFrame.idx = 0;
        currentFrame.size = 0;
    }

    ~BjEmit()
    {
        while(currentFrame.ty != FRM_SENTINEL)
        {
            currentFrame.currentKey.clear(mem);
            popFrame();
        }
        assert(frames.empty());
        frames.dealloc(mem);

        if(constants)
        {
            Var::ClearArray(mem, constants, constantsMax);
            constantsHolder.makeArrayUninitialized_Dangerous(mem, 0);
            constantsHolder.clear(mem);
        }

        outputValue.clear(mem);
    }

    bool finalize(Var& dst)
    {
        assert(currentFrame.ty == FRM_SENTINEL);
        dst = std::move(outputValue);
        return true;
    }

    bool Null() { return _emit(Var()); }
    bool Bool(bool b) { return _emit(b); }
    bool Int(int64_t i) { return _emit((s64)i); }
    bool Uint(uint64_t u) { return _emit((u64)u); }
    bool Double(double d) { return _emit(d); }
    bool Str(const char *s, size_t len) { return _emit(Var(mem, s, len)); }

    bool Array(size_t size)
    {
        Var v;
        Var *a = v.makeArray(mem, size);
        if(size && !a)
            return false;

        bool ret = _emit(std::move(v));
        if(size && ret) // empty arrays don't need to be pushed
        {
            pushFrame();
            currentFrame.ty = FRM_ARRAY;
            currentFrame.u.a = a;
            currentFrame.idx = 0;
            currentFrame.size = size;
        }
        return ret;
    }

    bool Map(size_t size)
    {
        Var v;
        Var::Map *m = v.makeMap(mem, size);
        if(!m)
            return false;

        bool ret = _emit(std::move(v));
        if(size && ret)
        {
            pushFrame();
            currentFrame.ty = FRM_MAP;
            currentFrame.u.m = m;
            currentFrame.idx = 0;
            currentFrame.size = size * 2;
        }
        return ret;
    }

    bool Constants(size_t start, size_t end)
    {
        assert(start < end);
        if(start == end)
            return true;
        if(start > constantsMax)
            return false;

        // this is the previously initialized/valid region that we're going to overwrite.
        // make it safe to placement-new into it.
        if(constants)
            Var::ClearArrayRange(mem, constants + start, constants + std::min(end, constantsMax));

        if(constantsMax < end)
        {
            constants = constantsHolder.makeArrayUninitialized_Dangerous(mem, end);
            if(!constants)
                return false;
        }

        pushFrame();
        currentFrame.ty = FRM_CTAB;
        currentFrame.u.a = NULL;
        currentFrame.idx = start;
        currentFrame.size = end;

        return true;
    }

    bool EmitConstant(size_t idx)
    {
        return idx < constantsMax
            && _emit(std::move(constants[idx].clone(mem, mem)));
    }

    void pushFrame()
    {
        frames.push_back(mem, std::move(currentFrame));
    }
    void popFrame()
    {
        currentFrame = std::move(frames.pop_back_move());
    }

    bool _emit(Var&& v)
    {
        const size_t idx = currentFrame.idx++;
        assert(!currentFrame.size || idx < currentFrame.size); // unknown size target has size=0

        switch(currentFrame.ty)
        {
            case FRM_ARRAY:
                currentFrame.u.a[idx] = std::move(v);
                //new(&f.u.a[idx]) Var(std::move(v)); // target is definitely uninitialized
                break;

            case FRM_MAP:
                if(idx & 1) // alternating key+value, keys are at even indices, values at odd
                {
                    assert(currentFrame.currentKey.type() == Var::TYPE_STRING);
                    currentFrame.u.m->put(mem, currentFrame.currentKey.asStrRef(), std::move(v));
                    currentFrame.currentKey.clear(mem);
                }
                else
                {
                    if(v.type() != Var::TYPE_STRING)
                        goto fail;
                    currentFrame.currentKey = std::move(v);
                }
                break;

            case FRM_CTAB:
                new(&constants[idx]) Var(std::move(v)); // target is possibly uninitialized
                constantsMax = std::max(constantsMax, idx+1);
                break;

            case FRM_SENTINEL:
                outputValue.clear(mem);
                outputValue = std::move(v);
                break;
        }

        // done all we wanted? this frame's finished, pop it
        if(idx + 1 == currentFrame.size)
            popFrame();

        return true;

    fail:
        v.clear(mem);
        return false;
    }

    /*
    bool ArrayUnk(size_t size) { return true; }
    bool MapUnk(size_t size) { return true; }
    bool Terminator() { return true; }
    */

    //bool _emit(Var&& v) { v.clear(mem); }

    Frame currentFrame; // storing this here is one pointer indirection less
    Var *constants = NULL;
    size_t constantsMax = 0;
    TreeMem& mem;

    LVector<Frame> frames;
    Var constantsHolder;
    Var outputValue;
};

static const size_t MaxSizeBits = sizeof(u64) * CHAR_BIT;


template<typename T>
static T _fail(T val, const char *msg)
{
    logerror("BJ loader failed: %s", msg);
    return val;
}

#define FAIL(ret, msg) _fail(ret, msg)
#define FAILMSG(msg) do { _fail(0, msg); } while(0)


#define UNREACHABLE() unreachable();

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
    OP_COPY_CONST
    // OP_UNUSED -- one more op possible here
};

// Note about OP_STRING:
// empty string encodes as 0x60 (`)
// this has the nice side effect that strings of length 1 begin with 'a' (0x61),
// length 2 with 'b' (0x62), and so on
// -> increases compressability since the byte that denotes the start of a string
//    stays in the range typically present in ascii strings anyway


inline static constexpr u8 encodeOp(Op op, u8 bits)
{
    return (op << 5) | bits;
}

// Chosen to be decode-able but effectively a nop when placed at the start of a bj stream.
// For format autodetection.
static const u8 s_magic[4] =
{
    encodeOp(OP_VALUE, 0b01000), // define constants
    0x80, 0,                     // two-byte encoding for index 0
    0                            // one-byte size=0 makes this an empty constant table,
                                 // and the decoder skips it.
};


bool checkMagic4(const char* p)
{
    return !memcmp(p, s_magic, sizeof(s_magic));
}

//-------------------------------------------------------


/* This reader does the bare minimal parsing work. Doesn't keep track of object boundaries and sizes,
* and only announces objects when they are created (so no closing callback). */
struct BjReader
{
    BjReader(BlockAllocator& alloc, BufferedReadStream& stream)
        : in(stream)
        , alloc(alloc)
        , remain(1)
    {}

    [[nodiscard]] inline bool expect(size_t n)
    {
        return !add_check_overflow(&remain, remain, n);
    }

    BufferedReadStream& in;
    size_t remain;
    BlockAllocator& alloc;

    //------------------------
    std::string tmpstr;
};


// helper to read a ULEB128-encoded uint. returns true when all is well.
// dst must already contain an initial value.
static bool _readnum_add(u64& dst, BufferedReadStream& in)
{
    u64 sh = 0;
    u8 c;
    goto beginloop;
    do
    {
        if(sh >= MaxSizeBits)
            return FAIL(false, "too many bits");
        sh += 7;
beginloop:
        if(in.done())
            return FAIL(false, "stream end");
        c = in.Take();
        const u64 add = u64(c & 0x7f) << sh;
        if(add_check_overflow(&dst, dst, add))
            return FAIL(false, "overflow");

    }
    while(c & 0x80);

    return true;
}

inline static bool readnum(u64& dst, BufferedReadStream& in)
{
    dst = 0;
    return _readnum_add(dst, in);
}

inline static bool smallnum5(u64& dst, BufferedReadStream& in, u8 a)
{
    dst = a;
    return a < 0b11111 || _readnum_add(dst, in);
}

// read value stored in little endian
template<typename T>
static bool readLE(T& dst, BufferedReadStream& in)
{
    union
    {
        u8 b[sizeof(T)];
        T x;
    } u;

    if(nativeendian.little)
    {
        if(in.availBuffered() >= sizeof(T)) // fast path: system is LE, and the stream has enough bytes
        {
            memcpy(&dst, in.ptr(), sizeof(T));
            in.advanceBuffered(sizeof(T));
            return true;
        }
        else
            for(size_t i = 0; i < sizeof(T); ++i)
            {
                if(in.done())
                    return FAIL(false, "stream end");
                u.b[i] = in.Take();
            }
    }
    else
    {
        for (size_t i = sizeof(T); i --> 0; )
        {
            if (in.done())
                return FAIL(false, "stream end");
            u.b[i] = in.Take();
        }
    }

    dst = u.x;
    return true;
}

// -------------------------------------------

template<typename Emit>
static bool readstrN(Emit& emit, BjReader& rd, size_t n)
{
    if(rd.in.availBuffered() >= n) // copy directly
    {
        bool ret = emit.Str((const char*)rd.in.ptr(), n);
        rd.in.advanceBuffered(n);
        return ret;
    }

    // slow path
    std::string& s = rd.tmpstr; // is a member to avoid repeated (re-)allocation
    s.resize(n);
    for(size_t i = 0; i < n; ++i)
    {
        if(rd.in.done())
            return FAIL(false, "stream end while reading string");
        s[i] = rd.in.Take();
    }
    return emit.Str(s.c_str(), n);
}


enum BjReadResult
{
    RD_FAIL  = 0,
    RD_OK    = 1, // lowest bit set
    RD_NOVAL = 2, // lowest bit not set
};


template<typename Emit>
static BjReadResult consttable(Emit& emit, BjReader& rd)
{
    u64 i, n;
    if(!readnum(i, rd.in))
        return FAIL(RD_FAIL, "def constants begin idx");
    if(!readnum(n, rd.in))
        return FAIL(RD_FAIL, "def constants size");
    if(!n) // ignore empty constant table
        return RD_NOVAL;
    u64 end;
    if(add_check_overflow(&end, i, n))
        return FAIL(RD_FAIL, "def constants size overflow");

    unsigned r = rd.expect(n) && emit.Constants(i, end);

    // either fail or noval
    return BjReadResult(r << 1u);
}


template<typename Emit>
static BjReadResult readval(Emit& emit, BjReader& rd)
{
    if(rd.in.done())
        return FAIL(RD_FAIL, "end of stream");
    u8 a = rd.in.Take();
    const Op op = Op(a >> 5);

    static_assert(OP_VALUE == 0, "no");
    if(op == OP_VALUE) // lower 5 bits have specialized encodings, upper 3 bits are known to be 0
    {
        if(!a) // None
            return BjReadResult(emit.Null());
        if((a & 0b11110) == 0b00010) // [0001x] Bool (lowest bit is true/false)
            return BjReadResult(emit.Bool(a & 1));
        if((a & 0b11100) == 0b00100) // [001xx] Float (lowest 2 bits decide how it's encoded)
        {
            double f = 0;
            switch(a & 0b11)
            {
                case 0: // Float32
                {
                    float ff;
                    if(readLE(ff, rd.in))
                        f = ff;
                    else
                        return FAIL(RD_FAIL, "read float32");
                }
                break;
                case 1: // Double
                    if (!readLE(f, rd.in))
                        return FAIL(RD_FAIL, "read double");
                break;
                case 2: // Read +int, cast to f
                {
                    u64 tmp;
                    if(!readnum(tmp, rd.in))
                        return FAIL(RD_FAIL, "read +int -> f");
                    f = (double)tmp;
                    break;
                }
                case 3: // Read -int, cast to f
                    u64 tmp;
                    if(!readnum(tmp, rd.in))
                        return FAIL(RD_FAIL, "read -int -> f");
                    f = -(double)tmp;
                    break;
            }
            return BjReadResult(emit.Double(f));
        }
        if(a == 0b01000) // [01000] Define constants
            return consttable(emit, rd);

        // Reserved:
        // 0b01xxx

        // Ideas:
        // 0b01001 - terminator
        // 0b01010 - array of unk len
        // 0b01011 - map of unk len

        // Unused / for user extensions:
        // 0b1xxxx

        return FAIL(RD_FAIL, "OP_VALUE unknown bits");
    }

    // some other op. the lower 5 bits are always a length.
    a &= 0b11111;
    u64 n;
    if(!smallnum5(n, rd.in, a))
        return FAIL(RD_FAIL, "num5 decode");

    switch(op)
    {
        case OP_COPY_CONST:
            return BjReadResult(emit.EmitConstant(n));

        case OP_STRING:
            return BjReadResult(readstrN(emit, rd, n));

        case OP_ARRAY:
            return BjReadResult(rd.expect(n) && emit.Array(n));

        case OP_MAP:
        {
            size_t e; // e = 2*n but with overflow check
            return BjReadResult(!add_check_overflow(&e, n, n) && rd.expect(e) && emit.Map(n));
        }

        case OP_INT_POS:
            return BjReadResult(emit.Uint(n));

        case OP_INT_NEG:
        {
            s64 neg = -s64(n);
            if (n && (s64(n) < 0) == (neg < 0)) // -n MUST flip the sign if != 0
                return FAIL(RD_FAIL, "OP_INT_NEG consistency");
            if (neg > 0) // underflow? But tolerate -0 (reads as simply 0 here)
                return FAIL(RD_FAIL, "OP_INT_NEG ended up > 0, should be negative");
            return BjReadResult(emit.Int(neg));
        }
    }

    return FAIL(RD_FAIL, "unhandled OP");
}


bool decode_json(VarRef dst, BufferedReadStream& src)
{
    BjReader rd(*dst.mem, src);
    BjEmit emit(*dst.mem);

    BjReadResult res;
    do
        res = readval(emit, rd);
    while(res && (rd.remain -= (res & 1)));

    if(res == RD_FAIL)
        return false;

    assert(res == RD_OK);
    return emit.finalize(*dst.v);
}

// -----------------------------------------------------------
// -----------------------------------------------------------
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

    ref2idx.reserve(*balloc, (u32)strcoll.size());

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
    wr.dst.Write((const char*)&s_magic[0], sizeof(s_magic)); // magic, for format autodetection
    size_t strsize = wr.poolAndEmitStrings();
    return 4 + strsize + encodeVal(wr, *json.v);
}



} // end namespace bj
