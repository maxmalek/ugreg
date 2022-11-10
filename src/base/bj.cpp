/* Inspired by BSON, BJSON, <insert whatever binary encoding for json here>.
   It's a draft but should be quite a bit more compact than all of the above.
   This code is possibly security-critical since it handles untrusted data.
   It has been AFL-fuzzed to death so beware nontrivial changes! */

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
#define FAILMSG(msg) do { puts(msg); } while(0)


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
static const char s_magic[4] =
{
    encodeOp(OP_VALUE, 0b01000), // define constants
    0,                           // at index 0
    1,                           // 1 constant
    encodeOp(OP_INT_NEG, 0)      // -0 (valid to decode but not used by encoder)
};


bool checkMagic4(const char* p)
{
    return !memcmp(p, s_magic, sizeof(s_magic));
}

//-------------------------------------------------------

enum BjFrameReadResult
{
    FRM_FAIL, // failed. exit.
    FRM_DONE, // no frame needed or frame is done, continue with next object
    FRM_PUSH, // caller pushed our current frame -> suspend
    FRM_NO_VALUE // no value was produced. continue right away
};

struct BjReadFrame;
struct BjReadState;

typedef BjFrameReadResult (*BjReadFrameFunc)(BjReadState& rd);
typedef void (*BjFailFrameFunc)(BjReadState& rd);

struct BjReadFrame
{
    BjReadFrameFunc f;
    Var *dst;
    size_t idx, n;
    BjFailFrameFunc fail;
};

struct BjReadState
{
    BjReadState(BufferedReadStream& src, TreeMem& mem, const Limits& lim) : src(src), mem(mem) {}

    BufferedReadStream& src;
    TreeMem& mem;
    const Limits lim;

    //------------------------
    BjReadFrame _currentFrame;
    Var constantsHolder;
    Var *constants = NULL;
    size_t constantsMax = 0;
    bool definingConstants = false;
    std::string tmpstr;
    LVector<BjReadFrame> _framestack;

    ~BjReadState()
    {
        assert(_framestack.empty());
        constantsHolder.clear(mem);
        _framestack.dealloc(mem);
    }

    BjReadFrame& beginFrame() // caller must set dst and write to that
    {
        _framestack.push_back(mem, std::move(_currentFrame));
        _currentFrame.dst = NULL;
        return _currentFrame;
    }
    BjReadFrame& currentFrame()
    {
        return _currentFrame; // this is intentionally always the same address
    }
    BjReadFrame& prevFrame() // returns frame below the top frame
    {
        return _framestack.back();
    }
    const BjReadFrame& popFrame() // returns new top frame after popping
    {
        _currentFrame = _framestack.pop_back_move();
        return _currentFrame;
    }
};

// helper to read a ULEB128-encoded uint. returns true when all is well.
// dst must already contain an initial value.
static bool _readnum_add(u64& dst, BjReadState& rd)
{
    u64 sh = 0;
    u8 c;
    do
    {
        if(rd.src.done())
            return FAIL(false, "stream end");
        if(sh >= MaxSizeBits)
            return FAIL(false, "too many bits");
        c = rd.src.Take();
        const u64 add = u64(c & 0x7f) << sh;
        if(add_check_overflow(&dst, dst, add))
            return FAIL(false, "overflow");
        sh += 7;
    }
    while((c & 0x80) && !rd.src.done());

    return true;
}

inline static bool readnum(u64& dst, BjReadState& rd)
{
    dst = 0;
    return _readnum_add(dst, rd);
}

inline static bool smallnum5(u64& dst, BjReadState& rd, u8 a)
{
    dst = a;
    return a < 0b11111 || _readnum_add(dst, rd);
}

// read value stored in little endian
template<typename T>
static bool readLE(T& dst, BjReadState& rd)
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

// ------------------- ARRAYS ------------------------

static BjFrameReadResult readval(Var& dst, BjReadState& rd);
static BjFrameReadResult readstrval(Var& dst, BjReadState& rd);

static void readArrayFail(BjReadState& rd)
{
    BjReadFrame& frm = rd.currentFrame();
    Var * const arr = frm.dst->array();
    assert(arr);
    // ouch. array is partially uninited. clear valid part, then drop array
    Var::ClearArray(rd.mem, arr, frm.idx);
    frm.dst->makeArrayUninitialized_Dangerous(rd.mem, 0);
    frm.dst->clear(rd.mem);
}

static BjFrameReadResult readArrayElems(BjReadState& rd)
{
    BjFrameReadResult res = FRM_DONE;
    Var tmp;

    // load state, resume there
    BjReadFrame& frm = rd.currentFrame(); // always ref to whichever frame is current
    const size_t len = frm.n;
    Var * const arr = frm.dst->array();
    assert(arr);
    size_t i = frm.idx;

    for( ; i < len; ++i)
    {
loopstart:
        res = readval(tmp, rd);
        switch(res)
        {
            case FRM_DONE:
                new (&arr[i]) Var(std::move(tmp)); // move into uninited mem
                break; // read next elem

            case FRM_PUSH: // readval() pushed a new frame.
                rd.prevFrame().idx = i+1; // resume here -- update old frame
                frm.dst = new (&arr[i]) Var(std::move(tmp));
                goto exit;

            case FRM_FAIL:
                FAILMSG("readArrayElems");
                frm.idx = i;
                tmp.clear(rd.mem);
                goto exit;

            case FRM_NO_VALUE:
                goto loopstart; // don't modify i, just read again
        }
    }
exit:
    assert(res != FRM_NO_VALUE);
    return res;
}

static BjFrameReadResult readArrayBegin(Var& dst, BjReadState& rd, size_t size)
{
    Var *arr = dst.makeArrayUninitialized_Dangerous(rd.mem, size);
    if(size) // don't need a frame for empty arrays
    {
        BjReadFrame& frm = rd.beginFrame(); // push a new frame, fill it in
        //frm.dst = &dst;
        frm.idx = 0;
        frm.n = size;
        frm.f = readArrayElems;
        frm.fail = readArrayFail;
        return arr ? FRM_PUSH : FRM_FAIL;
    }

    return FRM_DONE;
}

// --------------------- MAPS ----------------------

static void readSingleValueFail(BjReadState& rd)
{
    BjReadFrame& frm = rd.currentFrame();
    frm.dst->clear(rd.mem);
}

static BjFrameReadResult readMapElems(BjReadState& rd)
{
    BjFrameReadResult res = FRM_DONE;

    // load state, resume there
    BjReadFrame& frm = rd.currentFrame();
    const size_t len = frm.n;
    Var::Map * const m = frm.dst->map();
    assert(m);
    size_t i = frm.idx;
    const Var * const vecptr = m->values().data();

    Var k, v;

    for( ; i < len; ++i)
    {

        // 1) read key, must be a string as required by variant.h. never pushes a new frame.
        do
            res = readstrval(k, rd);
        while(res == FRM_NO_VALUE);
        if(res != FRM_DONE)
        {
            FAILMSG("readMapElems key");
            assert(res == FRM_FAIL);
            goto exit;
        }

        // 2) read value, anything is fine. may push a new frame.
readvalue:
        res = readval(v, rd);
        switch(res)
        {
            case FRM_DONE:
                m->put(rd.mem, k.asStrRef(), std::move(v));
                break; // read next elem

            case FRM_PUSH: // readval() pushed a new frame
                rd.prevFrame().idx = i+1; // resume here -- update old frame
                frm.dst = &m->put(rd.mem, k.asStrRef(), std::move(v));
                goto exit;

            case FRM_FAIL:
                FAILMSG("readMapElems value");
                v.clear(rd.mem);
                goto exit;

            case FRM_NO_VALUE:
                goto readvalue; // don't modify i, just read again
        }
    }
exit:
    assert(vecptr == m->values().data()); // Make sure the map didn't reallocate
    k.clear(rd.mem);
    assert(res != FRM_NO_VALUE);
    return res;
}

static BjFrameReadResult readMapBegin(Var& dst, BjReadState& rd, size_t size)
{
    Var::Map *m = dst.makeMap(rd.mem, size);
    if(size) // don't need a frame for empty maps
    {
        BjReadFrame& frm = rd.beginFrame(); // push a new frame, fill it in
        //frm.dst = &dst;
        frm.idx = 0;
        frm.n = size;
        frm.f = readMapElems;
        frm.fail = readSingleValueFail;
        return m ? FRM_PUSH : FRM_FAIL;
    }

    return FRM_DONE;
}

// -------------------- CONSTANT TABLE -----------------------

static void readConstantTableFail(BjReadState& rd, size_t validSize)
{
    Var::ClearArray(rd.mem, rd.constants, validSize); // destruct valid range (not actually dtor, but effectively clears everything into a dealloc-able state)
    rd.constants = rd.constantsHolder.makeArrayUninitialized_Dangerous(rd.mem, 0); // dealloc without dtor, including any uninitialized tail
    assert(!rd.constants);
    rd.constantsHolder.clear(rd.mem); // to be safe: do this here so that we won't crash later in the dtor in case anything is wrong
}

static void readConstantTableFail(BjReadState& rd)
{
    BjReadFrame& frm = rd.currentFrame();
    readConstantTableFail(rd, frm.idx);
}


static BjFrameReadResult readConstantTableElems(BjReadState& rd)
{
    BjFrameReadResult res = FRM_DONE;
    Var tmp;

    // load state, resume there
    BjReadFrame& frm = rd.currentFrame();
    size_t i = frm.idx;
    const size_t end = frm.n;
    Var * const arr = rd.constantsHolder.array();
    assert(arr);

    for( ; i < end; ++i)
    {
        res = readval(tmp, rd);
        switch(res)
        {
            case FRM_DONE:
                new (&arr[i]) Var(std::move(tmp)); // move into uninited mem
                rd.constantsMax = std::max(rd.constantsMax, i+1);  // constant written successfully, can now use it
                break; // read next elem

            case FRM_PUSH:
                rd.prevFrame().idx = i+1; // resume here -- update current state in case we get pushed
                frm.dst = new (&arr[i]) Var(std::move(tmp)); // a new frame was pushed but frm (aka rd._currentFrame) didn't change and now holds the NEW FRAME, so we can keep using this
                goto exit;

            case FRM_FAIL:
                tmp.clear(rd.mem);
                return FAIL(FRM_FAIL, "readConstantTableElems");

            case FRM_NO_VALUE:
                UNREACHABLE(); // can't nest constant tables
        }
    }
    rd.definingConstants = false; // clear this ONLY when done processing all elements is done aka after exiting the loop cleanly
    res = FRM_NO_VALUE; // we didn't actually produce a value, caller must read again
exit:
    assert(res != FRM_DONE); // this function does not produce a value
    return res;
}

static BjFrameReadResult readConstantTableBegin(BjReadState& rd, size_t begin, size_t end)
{
    if(end > rd.lim.constants)
        return FAIL(FRM_FAIL, "def constants table exceeds limits");
    if(begin > rd.constantsMax)
        return FAIL(FRM_FAIL, "def constants non-contiguous");
    if(rd.definingConstants)
        return FAIL(FRM_FAIL, "can't nest def constants");

    Var& ctab = rd.constantsHolder;
    const size_t oldsize = ctab._size();
    assert(oldsize == rd.constantsMax);

    // we know that n elems follow. so we don't have to init array elems only to overwrite them later
    // it's possible that malicious inputs request a large block of memory,
    // in that case we might fail later when trying to read elements and the input stream runs out.
    if(oldsize < end)
    {
        // precond: arr[0..oldsize) is initialized and valid
        Var * const arr = ctab.makeArrayUninitialized_Dangerous(rd.mem, end);
        if(!arr)
        {
            readConstantTableFail(rd, oldsize);
            return FAIL(FRM_FAIL, "def constants (alloc)");
        }
        rd.constants = arr;
    }

    if(begin != end) // don't need a frame for empty constant table
    {
        // this is the range that will be overwritten/redefined
        Var::ClearArrayRange(rd.mem, rd.constants + begin, rd.constants + oldsize);

        BjReadFrame& frm = rd.beginFrame(); // push a new frame, fill it in
        frm.dst = &rd.constantsHolder; // not actually used, but serves as a marker (**)
        frm.idx = begin;
        frm.n = end;
        frm.f = readConstantTableElems;
        frm.fail = readConstantTableFail;

        rd.definingConstants = true;
        return FRM_PUSH;
    }

    return FRM_NO_VALUE; // empty constant table
}

// -------------------------------------------

static BjFrameReadResult readstrN(Var& dst, BjReadState& rd, size_t n)
{
    if(rd.src.availBuffered() >= n) // copy directly
    {
        dst.setStr(rd.mem, (const char*)rd.src.ptr(), n);
        rd.src.advanceBuffered(n);
    }
    else // slow path
    {
        std::string& s = rd.tmpstr; // is a member to avoid repeated (re-)allocation
        s.resize(n);
        for(size_t i = 0; i < n; ++i)
        {
            if(rd.src.done())
                return FAIL(FRM_FAIL, "stream end while reading string");
            s[i] = rd.src.Take();
        }
        dst.setStr(rd.mem, s.c_str(), n);
    }
    return FRM_DONE;
}

static BjFrameReadResult copyconst(Var& dst, BjReadState& rd, size_t idx)
{
    if(idx < rd.constantsMax)
    {
        const Var& c = rd.constants[idx];
        dst.clear(rd.mem);
        dst = std::move(c.clone(rd.mem, rd.mem));
        return FRM_DONE;
    }
    return FAIL(FRM_FAIL, "OP_COPY_CONST index out of bounds");
}

static BjFrameReadResult consttable(BjReadState& rd)
{
    u64 i, n;
    if(!readnum(i, rd))
        return FAIL(FRM_FAIL, "def constants begin idx");
    if(!readnum(n, rd))
        return FAIL(FRM_FAIL, "def constants size");
    u64 end;
    if(add_check_overflow(&end, i, n))
        return FAIL(FRM_FAIL, "def constants size overflow");

    return readConstantTableBegin(rd, i, end);
}

// like readval(), but fails when the value is not a string. for reading map keys.
// never returns FRM_PUSH.
static BjFrameReadResult readstrval(Var& dst, BjReadState& rd)
{
    if(rd.src.done())
        return FAIL(FRM_FAIL, "readstrval end of stream");
    u8 a = rd.src.Take();
    const Op op = Op(a >> 5);
    /*if(op == OP_VALUE)
    {
        if(a == 0b01000) // Define constants
            return consttable(rd);
    }
    else*/
    {
        a &= 0b11111;

        u64 n;
        if(!smallnum5(n, rd, a))
            return FAIL(FRM_FAIL, "readstrval num5 decode");

        switch(op)
        {
            case OP_STRING:
                return readstrN(dst, rd, n);
            case OP_COPY_CONST:
                if(copyconst(dst, rd, n) == FRM_DONE && dst.type() == Var::TYPE_STRING)
                    return FRM_DONE;
        }
    }
    return FAIL(FRM_FAIL, "readstrval not a string");
}

// read one value. may push a new frame.
// protocol:
// when FRM_DONE is returned, dst was written do.
// when FRM_PUSH is returned, dst is initialized with a valid (but yet unfilled) map/array, unless we're defining constants
// if we're defining constants, dst is a nullref (technically invalid but we're not touching it, so whatever), and the frame is popped without a write
static BjFrameReadResult readval(Var& dst, BjReadState& rd)
{
    if(rd.src.done())
        return FAIL(FRM_FAIL, "end of stream");
    u8 a = rd.src.Take();
    const Op op = Op(a >> 5);

    if(!op) // OP_VALUE -- lower 5 bits have specialized encodings, upper 3 bits are known to be 0
    {
        if(!a) // None
        {
            dst.clear(rd.mem);
            return FRM_DONE;
        }
        if((a & 0b11110) == 0b00010) // Bool
        {
            dst.setBool(rd.mem, a & 1);
            return FRM_DONE;
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
                        return FAIL(FRM_FAIL, "read float32");
                }
                break;
                case 1: // Double
                    if (!readLE(f, rd))
                        return FAIL(FRM_FAIL, "read double");
                break;
                case 2: // Read +int, cast to f
                {
                    u64 tmp;
                    if(!readnum(tmp, rd))
                        return FAIL(FRM_FAIL, "read +int -> f");
                    f = (double)tmp;
                    break;
                }
                case 3: // Read -int, cast to f
                    u64 tmp;
                    if(!readnum(tmp, rd))
                        return FAIL(FRM_FAIL, "read -int -> f");
                    f = -(double)tmp;
                    break;
            }
            dst.setFloat(rd.mem, f);
            return FRM_DONE;
        }
        if(a == 0b01000) // Define constants
            return consttable(rd);

        // Reserved:
        // 0b01xxx

        // Ideas:
        // 0b01001 - terminator
        // 0b01010 - array of unk len
        // 0b01011 - map of unk len

        // Unused / for user extensions:
        // 0b1xxxx

        return FAIL(FRM_FAIL, "OP_VALUE unknown bits");
    }

    // some other op. the lower 5 bits are always a length.
    a &= 0b11111;
    u64 n;
    if(!smallnum5(n, rd, a))
        return FAIL(FRM_FAIL, "num5 decode");

    switch(op)
    {
        case OP_INT_POS:
        {
            dst.setUint(rd.mem, n);
            return FRM_DONE;
        }
        case OP_INT_NEG:
        {
            s64 neg = -s64(n);
            if(n && (s64(n) < 0) == (neg < 0)) // -n MUST flip the sign if != 0
                return FAIL(FRM_FAIL, "OP_INT_NEG consistency");
            if(neg > 0) // underflow? But tolerate -0 (reads as simply 0 here)
                return FAIL(FRM_FAIL, "OP_INT_NEG ended up > 0, should be negative");
            dst.setInt(rd.mem, neg);
            return FRM_DONE;
        }
        case OP_STRING:
        {
            if(n > rd.lim.maxsize) // FIXME: this and the next few should be solved via limiting the memory allocator, not per-object
                return FAIL(FRM_FAIL, "string size exceeds limit");

            return readstrN(dst, rd, n);
        }
        case OP_ARRAY:
        {
            if(n > rd.lim.maxsize)
                return FAIL(FRM_FAIL, "OP_ARRAY size exceeds limit");

            return readArrayBegin(dst, rd, n);
        }
        case OP_MAP:
        {
            if(n > rd.lim.maxsize)
                return FAIL(FRM_FAIL, "OP_MAP size exceeds limit");

            return readMapBegin(dst, rd, n);
        }
        case OP_COPY_CONST:
            return copyconst(dst, rd, n);
    }

    return FAIL(FRM_FAIL, "unhandled OP");
}

// to begin the iteration and to init the first frame
static BjFrameReadResult readInitialSingleValue(BjReadState& rd)
{
    BjReadFrame& frm = rd.currentFrame();
    Var *dst = frm.dst;
    BjFrameReadResult res;
    do
        res = readval(*dst, rd);
    while(res == FRM_NO_VALUE);
    if(res == FRM_PUSH && !frm.dst)
        frm.dst = dst; // new frame gets our root node

    assert(res != FRM_NO_VALUE);
    return res;
}


bool decode_json(VarRef dst, BufferedReadStream& src, const Limits& lim)
{
    Var tmp; // keep this in a local until we're sure the entire thing was read in without errors
    BjReadState rd(src, *dst.mem, lim);
    // init the bottom frame so that a later pushFrame() doesn't try to clone something entirely invalid
    BjReadFrame& frm = rd.currentFrame();
    frm.dst = &tmp;
    frm.f = readInitialSingleValue;
    frm.fail = readSingleValueFail;

    for(;;)
    {
        switch(frm.f(rd))
        {
            case FRM_DONE:
            case FRM_NO_VALUE:
                if(!rd._framestack.empty())
                {
                    rd.popFrame(); // continue with next frame
                    break;
                }
                dst.clear(); // everything finished, assign output properly
                *dst.v = std::move(tmp);
                return true;
            case FRM_PUSH:
                assert(!rd._framestack.empty()); // something was pushed, so this can't be empty
                break; // got a new frame on top, continue with it. (frm was modified)

            case FRM_FAIL:
                // top frame failed. fail all frames that still exist to unroll cleanly
                while(!rd._framestack.empty())
                {
                    frm.fail(rd);
                    rd.popFrame();
                }
                frm.fail(rd); // last frame
                return false;
        }
    }
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
    wr.dst.Write(s_magic, sizeof(s_magic)); // magic, for format autodetection
    size_t strsize = wr.poolAndEmitStrings();
    return 4 + strsize + encodeVal(wr, *json.v);
}



} // end namespace bj
