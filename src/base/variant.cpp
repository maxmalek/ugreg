#include <stdlib.h>
#include <assert.h>
#include <utility>
#include <limits>
#include <string.h>
#include <float.h>

#include "variant.h"
#include "treemem.h"
#include "mem.h"
#include "util.h"

const Var Var::Null;

static _VarMap*_NewMap(TreeMem& mem, size_t prealloc)
{
    (void)prealloc; // TODO: use this
    void *p = mem.Alloc(sizeof(_VarMap));
    return _X_PLACEMENT_NEW(p) _VarMap(mem);
}
// ... use m->destroy(mem) to delete it

static Var *_NewArray(TreeMem& mem, size_t n)
{
    Var* p = (Var*)mem.Alloc(sizeof(Var) * n);
    mem_construct_default(p, p + n);
    return p;
}

static Var* _ResizeArray(TreeMem& mem, size_t newsize, Var *arr, size_t oldsize)
{
    if(newsize == oldsize) // sanity check
        return arr;

    // shrinking? -- destruct trailing range first
    if(newsize < oldsize)
    {
        for(size_t i = newsize; i < oldsize; ++i)
            arr[i].clear(mem);
        mem_destruct(arr + newsize, arr + oldsize);
    }
    Var* p = (Var*)mem.Realloc(arr, sizeof(Var) * oldsize, sizeof(Var) * newsize);
    // enlarging?
    if(oldsize < newsize)
        mem_construct_default(p + oldsize, p + newsize);
    return p;
}

static void _DeleteArray(TreeMem& mem, Var *p, size_t n)
{
    assert(!p == !n);
    if(p)
    {
        for(size_t i = 0; i < n; ++i)
            p[i].clear(mem);
        mem_destruct(p, p + n);
        mem.Free(p, sizeof(*p) * n);
    }
}

static _VarExtra*_NewExtra(TreeMem& mem, _VarMap& m)
{
    void *p = (_VarExtra*)mem.Alloc(sizeof(_VarExtra));
    _VarExtra*ex = _X_PLACEMENT_NEW(p) _VarExtra(m, mem);
    return ex;
}

static void _DeleteExtra(TreeMem& mem, _VarExtra *ex)
{
    assert(&ex->mem == &mem);
    ex->~_VarExtra();
    mem.Free(ex, sizeof(*ex));
}

static _VarRange* _NewRanges(TreeMem& mem, size_t n)
{
    size_t memsz = sizeof(_VarRange) * (n+1);
    _VarRange* ra = (_VarRange*)mem.Alloc(memsz);
    ra[0].first = n;
    ra[0].last = memsz;
    return ra + 1;
}

static void _DeleteRanges(TreeMem& mem, _VarRange* ra)
{
    _VarRange *p = &ra[-1];
    size_t memsz = p->last;
    mem.Free(p, memsz);
}

inline static size_t _RangeLen(_VarRange *ra)
{
    return ra[-1].first;
}


Var::Var()
    : meta(TYPE_NULL)
    // .u does not need to be inited here
{
    //u.p = NULL; // not necessary
    static_assert((size_t(1) << (SHIFT_TOPBIT - 1u)) - 1u == SIZE_MASK,
        "SIZE_MASK might be weird, check this");
}

Var::~Var()
{
    // We can't get a TreeMem at this point. Since we don't store it either,
    // we're screwed if there's still some leftover memory in the dtor.
    // Means if we have a ref to an external block of memory, we have no other
    // choice than to leak it.
    // Practically it will be cleared when the TreeMem is destroyed, but
    // that may not happen until process exit, at which point it doesn't matter anyway.
    // TL;DR if this blows, did you forget to clear()?
    assert(!meta && "Var not cleared before dtor!");
}

Var::Var(Var&& v) noexcept
    : meta(v.meta), u(v.u)
{
    v.meta = TYPE_NULL;
    //v.u.p = NULL; // not necessary
}

Var& Var::operator=(Var&& o) noexcept
{
    assert(meta == TYPE_NULL); // Forgot to clear() first?
    meta = o.meta;
    u = o.u;
    o.meta = TYPE_NULL;
    //o.u.p = NULL; // not necessary
    return *this;
}

Var::Var(bool x)
    : meta(TYPE_BOOL)
{
    u.ui = x;
}

Var::Var(s64 x)
    : meta(TYPE_INT)
{
    u.i = x;
}

Var::Var(u64 x)
    : meta(TYPE_UINT)
{
    u.ui = x;
}

Var::Var(double x)
    : meta(TYPE_FLOAT)
{
    u.f = x;
}

Var::Var(TreeMem& mem, const char* s)
    : Var()
{
    setStr(mem, s);
}

Var::Var(TreeMem& mem, const char* s, size_t len)
    : Var()
{
    setStr(mem, s, len);
}

void Var::_settop(TreeMem& mem, Topbits top, size_t size)
{
    assert(size <= SIZE_MASK);
    assert(top != BITS_OTHER);
    _transmute(mem, (size_t(top) << SHIFT_TOP2BITS) | (size & SIZE_MASK));
}

void Var::_transmute(TreeMem& mem, size_t newmeta)
{
    switch(_topbits())
    {
        case BITS_STRING:
            mem.freeS(u.s);
            break;
        case BITS_ARRAY:
            _DeleteArray(mem, u.a, _size());
            break;
        case BITS_MAP:
            u.m->destroy(mem);
            break;
        case BITS_OTHER:
            if(meta == TYPE_RANGE)
            {
                _DeleteRanges(mem, u.ra);
                u.ra = NULL;
            }
            break;
    }

    meta = newmeta;
}

void Var::_adjustsize(size_t size)
{
    assert(size <= SIZE_MASK);
    assert(_topbits() == BITS_ARRAY || _topbits() == BITS_STRING); // makes no sense for the other types
    meta = (meta & ~SIZE_MASK) | (size & SIZE_MASK);
}

void Var::clear(TreeMem& mem)
{
    _transmute(mem, TYPE_NULL);
}

Var Var::clone(TreeMem &dstmem, const TreeMem& srcmem) const
{
    Var dst;
    switch(_topbits())
    {
    case BITS_STRING:
    {
        PoolStr ps = srcmem.getSL(u.s);
        dst.u.s = dstmem.put(ps.s, ps.len);
        break;
    }
    case BITS_ARRAY:
    {
        const size_t N = _size();
        Var * const a = _NewArray(dstmem, N);
        for(size_t i = 0; i < N; ++i)
            a[i] = std::move(u.a[i].clone(dstmem, srcmem));
        dst.u.a = a;
        break;
    }
    case BITS_MAP:
        dst.u.m = u.m->clone(dstmem, srcmem);
        break;
    case BITS_OTHER:
        switch(meta)
        {
            case TYPE_RANGE:
                dst.setRange(dstmem, u.ra, _RangeLen(u.ra));
                break;
            default:
                dst.u = this->u;
                break;
        }
        break;
    }
    dst.meta = this->meta;
    return dst;
}

Var * Var::makeArray(TreeMem& mem, size_t n)
{
    Var *a;
    if(_topbits() == BITS_ARRAY)
    {
        a = _ResizeArray(mem, n, u.a, _size());
        _adjustsize(n);
    }
    else
    {
        a = _NewArray(mem, n);
        _settop(mem, BITS_ARRAY, n);
    }
    return (( u.a = a )); // u.a must be assigned AFTER _settop()!
}

Var::Map *Var::makeMap(TreeMem& mem, size_t prealloc)
{
    if (_topbits() == BITS_MAP)
        return u.m;
    _settop(mem, BITS_MAP, 0);
    return (( u.m = _NewMap(mem, prealloc) ));
}

Var::Range* Var::setRange(TreeMem& mem, const Range *ra, size_t n)
{
    Range *p = _NewRanges(mem, n);
    if(ra)
        memcpy(p, ra, sizeof(*ra) * n);
    _transmute(mem, TYPE_RANGE);
    return (( u.ra = p ));
}

const s64 *Var::asInt() const
{
    switch (meta)
    {
    case TYPE_UINT:
        if (u.ui > u64(std::numeric_limits<s64>::max()))
            break;
        [[fallthrough]];
        case TYPE_INT:
            return &u.i;
    }
    return NULL;
}

const u64 *Var::asUint() const
{
    switch(meta)
    {
        case TYPE_INT:
            if(u.i < 0)
                break;
        [[fallthrough]];
        case TYPE_UINT:
            return &u.ui;
    }
    return NULL;
}

StrRef Var::asStrRef() const
{
    return _topbits() == BITS_STRING ? u.s : 0;
}

const double *Var::asFloat() const
{
    return meta == TYPE_FLOAT ? &u.f : NULL;
}

bool Var::asBool() const
{
    switch(meta)
    {
        case TYPE_BOOL:
        case TYPE_INT:
        case TYPE_UINT:
            return !!u.ui;
    }
    return false;
}

void* Var::asPtr() const
{
    return meta == TYPE_PTR ? u.p : NULL;
}

const Var::Range *Var::asRange() const
{
    return meta == TYPE_RANGE ? u.ra : NULL;
}

PoolStr Var::asString(const TreeMem& mem) const
{
    PoolStr ps { NULL, 0 };
    if(_topbits() == BITS_STRING)
    {
        ps.s = mem.getS(u.s);
        ps.len = _size();
        assert(ps.len == mem.getL(u.s));
    }
    return ps;
}

const char* Var::asCString(const TreeMem& mem) const
{
    return _topbits() == BITS_STRING ? mem.getS(u.s) : NULL;
}

const Var *Var::at(size_t idx) const
{
    return _topbits() == BITS_ARRAY && idx < _size() ? &u.a[idx] : NULL;
}

Var *Var::at(size_t idx)
{
    return _topbits() == BITS_ARRAY && idx < _size() ? &u.a[idx] : NULL;
}

Var& Var::operator[](size_t idx)
{
    assert(_topbits() == BITS_ARRAY);
    assert(idx < _size());
    return u.a[idx];
}

const Var& Var::operator[](size_t idx) const
{
    assert(_topbits() == BITS_ARRAY);
    assert(idx < _size());
    return u.a[idx];
}

Var* Var::array_unsafe()
{
    assert(_topbits() == BITS_ARRAY);
    return u.a;
}

const Var* Var::array_unsafe() const
{
    assert(_topbits() == BITS_ARRAY);
    return u.a;
}

Var::Map *Var::map_unsafe()
{
    assert(_topbits() == BITS_MAP);
    return u.m;
}

const Var::Map *Var::map_unsafe() const
{
    assert(_topbits() == BITS_MAP);
    return u.m;
}

Var* Var::lookup(StrRef k)
{
    return _topbits() == BITS_MAP ? u.m->get(k) : NULL;
}

const Var* Var::lookup(StrRef k) const
{
    return _topbits() == BITS_MAP ? u.m->get(k) : NULL;
}

Var::Type Var::type() const
{
    static const Type types[] = { TYPE_NULL, TYPE_STRING, TYPE_ARRAY, TYPE_MAP }; // first element is never used
    size_t b = _topbits();
    return b ? types[b] : (Type)meta;
}

static const char *s_typeNames[] =
{
    "null",
    "bool",
    "int",
    "uint",
    "float",
    "ptr",
    "range",
    "string",
    "array",
    "map"
    // keep in sync with enum Type in the .h file!
};

const char *Var::typestr() const
{
    Type t = type();
    assert(t < Countof(s_typeNames));
    return s_typeNames[t];
}

template<int P, typename T, Var::Type t>
struct TypeHolder
{
    typedef T type;
    static const Var::Type enumtype = t;
    enum { priority = P };
};
template<typename T> struct FromCType;
template<> struct FromCType<std::nullptr_t> : TypeHolder<0, std::nullptr_t, Var::TYPE_NULL> {};
template<> struct FromCType<bool>           : TypeHolder<1, bool, Var::TYPE_BOOL> {};
template<> struct FromCType<u64>            : TypeHolder<2, u64, Var::TYPE_UINT> {};
template<> struct FromCType<s64>            : TypeHolder<3, s64, Var::TYPE_INT> {};
template<> struct FromCType<double>         : TypeHolder<4, double, Var::TYPE_FLOAT> {};

template<Var::Type t> struct FromEType;
template<> struct FromEType<Var::TYPE_NULL>  : FromCType<std::nullptr_t> {};
template<> struct FromEType<Var::TYPE_BOOL>  : FromCType<bool> {};
template<> struct FromEType<Var::TYPE_UINT>  : FromCType<u64> {};
template<> struct FromEType<Var::TYPE_INT>   : FromCType<s64> {};
template<> struct FromEType<Var::TYPE_FLOAT> : FromCType<double> {};

template<typename A, typename B>
struct LargerOfBoth
{
    typedef typename std::conditional<
        (FromCType<A>::priority > FromCType<B>::priority),
        A,
        B
    >::type type;
};

template<typename A, typename B>
struct NumericCompare3
{
    inline static int cmp(A a, B b)
    {
        return a < b ? -1 : (a == b ? 0 : 1);
    }
};

template<>
struct NumericCompare3<s64, u64>
{
    inline static int cmp(s64 a, u64 b)
    {
        if(a < 0)
            return -1; // b is unsigned and can never be greater
        return NumericCompare3<u64, u64>::cmp(a, b);
    }
};

template<typename A, typename B, bool swap>
struct NumericCompare2 {};

template<typename A, typename B>
struct NumericCompare2<A, B, false>
{
    static_assert(std::is_same<A, typename LargerOfBoth<A, B>::type>::value, "oops");
    inline static int cmp(A a, B b)
    {
        return NumericCompare3<A, B>::cmp(a, b);
    }
};

template<typename A, typename B>
struct NumericCompare2<A, B, true>
{
    static int cmp(A a, B b)
    {
        return -(NumericCompare2<B, A, false>::cmp(b, a));
    }
};

// make it so that A is always the larger type of the two
template<typename A, typename B>
struct NumericCompare : NumericCompare2<A, B, (FromCType<A>::priority < FromCType<B>::priority)>
{
};

template<typename A, typename B>
static inline int numericCmpT(A a, B b)
{
    return NumericCompare<A, B>::cmp(a, b);
}

template<typename T>
struct GetValue
{};

template<>
struct GetValue<u64>
{
    static u64 get(const Var& v) { return v.u.ui; }
};

template<>
struct GetValue<bool>
{
    static bool get(const Var& v) { return v.u.ui; }
};

template<>
struct GetValue<s64>
{
    static s64 get(const Var& v) { return v.u.i; }
};

template<>
struct GetValue<double>
{
    static double get(const Var& v) { return v.u.f; }
};

template<Var::Type A, Var::Type B>
static inline int numericCmp(const Var& a, const Var& b)
{
    typedef typename FromEType<A>::type TA;
    typedef typename FromEType<B>::type TB;
    return NumericCompare<TA, TB>::cmp(GetValue<TA>::get(a), GetValue<TB>::get(b));
}
int Var::numericCompare(const Var& b) const
{
    const Var::Type at = type();
    const Var::Type bt = b.type();

#define $(A, B) if(at == A && bt == B) return (numericCmp<A, B>(*this, b))
    // same type
    $(TYPE_UINT, TYPE_UINT);
    $(TYPE_INT, TYPE_INT);
    $(TYPE_FLOAT, TYPE_FLOAT);
    // different types
    $(TYPE_FLOAT, TYPE_INT);
    $(TYPE_FLOAT, TYPE_UINT);
    $(TYPE_INT, TYPE_FLOAT);
    $(TYPE_INT, TYPE_UINT);
    $(TYPE_UINT, TYPE_INT);
    $(TYPE_UINT, TYPE_FLOAT);
#undef $

    assert(false);
    return 0;
}

bool Var::equals(const TreeMem& mymem, const Var& o, const TreeMem& othermem) const
{
    if(this == &o)
        return true;

    const Type myt = type();
    const Type ot = o.type();

    if(myt != ot)
    {
        if ((myt == TYPE_INT || myt == TYPE_UINT || myt == TYPE_FLOAT)
            && (ot == TYPE_INT || ot == TYPE_UINT || ot == TYPE_FLOAT))
            return numericCompare(o) == 0;

        return false;
    }

    // type is equal, now check what it is
    switch(myt)
    {
        case TYPE_NULL:
            return true;
        case TYPE_INT:
            return u.i == o.u.i;
        case TYPE_UINT:
        case TYPE_BOOL:
            return u.ui == o.u.ui;
        case TYPE_PTR:
            return u.p == o.u.p;
        case TYPE_FLOAT:
            return abs(u.f - o.u.f) < 0.001; // some leeway with precision but this should be close enough, arbitrarily
        case TYPE_STRING:
        {
            if(&mymem == &othermem)
                return u.s == o.u.s;
             PoolStr s = asString(mymem);
             PoolStr os = o.asString(othermem);
             return s.len == os.len && !memcmp(s.s, os.s, s.len);
        }
        case TYPE_ARRAY:
        {
            const size_t n = _size();
            if(n != o._size())
                return false;
            const Var *a = array();
            const Var *oa = o.array();
            for(size_t i = 0; i < n; ++i)
                if(!a[i].equals(mymem, oa[i], othermem))
                    return false;
            return true;
        }

        case TYPE_MAP:
            return map()->equals(mymem, *o.map(), othermem);
    }
    assert(false); // unreachable
    return false;
}



Var::CompareResult Var::compare(CompareMode cmp, const TreeMem& mymem, const Var& o, const TreeMem& othermem) const
{
    if(cmp == CMP_EQ)
        return CompareResult(equals(mymem, o, othermem));

    const Type myt = type();
    const Type ot = o.type();

    // numeric comparison
    if(cmp == CMP_LT || cmp == CMP_GT)
    {
        if((myt == TYPE_INT || myt == TYPE_UINT || myt == TYPE_FLOAT)
        && (ot == TYPE_INT || ot == TYPE_UINT || ot == TYPE_FLOAT))
        {
            int c = numericCompare(o);
            return CompareResult(cmp == CMP_LT ? c < 0 : c > 0);
        }
        return CMP_RES_NA;
    }

    // string comparisons
    if(myt == TYPE_STRING && ot == TYPE_STRING)
    {
        const PoolStr pa = asString(mymem);
        const PoolStr pb = o.asString(othermem);

        if (pa.len < pb.len) // a must be at least as long as b, or longer
            return CMP_RES_FALSE;

        switch(cmp)
        {
            case CMP_STARTSWITH: // a starts with b
                return CompareResult(!strncmp(pa.s, pb.s, pb.len));

            case CMP_ENDSWITH: // a ends with b
                return CompareResult(!strncmp(pa.s + (pa.len - pb.len), pb.s, pb.len));

            case CMP_CONTAINS: // a contains b
                return CompareResult(!!strstr(pa.s, pb.s));

            default:
                assert(false);
        }
    }

    return CMP_RES_NA;
}

size_t Var::size() const
{
    switch(_topbits())
    {
        case 0: // Not a container
            switch(meta)
            {
                case TYPE_RANGE:
                    return _RangeLen(u.ra);
                default:
                    return 0;
            }
        case BITS_MAP: // Map stores size on its own while the meta field is empty
            return u.m->size();
        default:
            return _size(); // array & string know their size
    }
}

bool Var::setBool(TreeMem& mem, bool x)
{
    _transmute(mem, TYPE_BOOL);
    return (( u.ui = x ));
}

s64 Var::setInt(TreeMem& mem, s64 x)
{
    _transmute(mem, TYPE_INT);
    return (( u.i = x ));
}

u64 Var::setUint(TreeMem& mem, u64 x)
{
    _transmute(mem, TYPE_UINT);
    return ((u.ui = x));
}

double Var::setFloat(TreeMem& mem, double x)
{
    _transmute(mem, TYPE_FLOAT);
    return (( u.f = x ));
}

StrRef Var::setStr(TreeMem& mem, const char* x)
{
    return setStr(mem, x, strlen(x));
}

StrRef Var::setStr(TreeMem& mem, const char* x, size_t len)
{
    StrRef s = mem.put(x, len);
    assert(s);
    _settop(mem, BITS_STRING, len);
    return (( u.s = s ));
}

void* Var::setPtr(TreeMem& mem, void* p)
{
    _transmute(mem, TYPE_PTR);
    return ((u.p = p));
}

const Var::Extra* Var::getExtra() const
{
    const Map *m = map();
    return m ? m->getExtra() : NULL;
}

Var::Extra* Var::getExtra()
{
    const Map* m = map();
    return m ? m->getExtra() : NULL;
}

void _VarMap::_checkmem(const TreeMem& m) const
{
#ifdef _DEBUG
    assert(_mymem == &m && "Allocator mismatch!");
#endif
}

_VarMap::_VarMap(TreeMem& mem)
    : _storage(mem)
    , _extra(NULL)
#ifdef _DEBUG
    , _mymem(&mem)
#endif
{
}

_VarMap::_VarMap(_VarMap&& o) noexcept
    : _storage(std::move(o._storage))
    , _extra(o._extra)
#ifdef _DEBUG
    , _mymem(o._mymem)
#endif
{
    o._extra = NULL;
}

_VarMap::~_VarMap()
{
    assert(_storage.empty()); // Same thing as in ~Var()
}

void _VarMap::destroy(TreeMem& mem)
{
    clear(mem);
    this->~_VarMap();
    mem.Free(this, sizeof(*this));
}

Var& _VarMap::_InsertAndRefcount(TreeMem& dstmem, _Map& storage, StrRef k)
{
    // Create key/value if it's not there yet
    _Map::InsertResult r = storage.insert_new(dstmem, k);
    if (r.newly_inserted)
        dstmem.increfS(k); // ... and refcount it if newly inserted
    return r.ref;
}

bool _VarMap::equals(const TreeMem& mymem, const _VarMap& o, const TreeMem& othermem) const
{
    _checkmem(mymem);
    o._checkmem(othermem);

    if(this == &o)
        return true;
    if(size() != o.size())
        return false;

    // If the memory pool is the same, we can use the string refs as-is
    if(&mymem == &othermem)
    {
        for (Iterator it = begin(); it != end(); ++it)
        {
            const Var *oi = o._storage.getp(it.key());
            if (!oi)
                return false;
            if (!it.value().equals(mymem, *oi, othermem))
                return false;
        }
    }
    else
    {
        for(Iterator it = begin(); it != end(); ++it)
        {
            // Different mem pool, translate string IDs
            const PoolStr ps = mymem.getSL(it.key());
            StrRef oref = othermem.lookup(ps.s, ps.len);
            const Var *oi = o._storage.getp(oref);
            if(!oi)
                return false;
            if(!it.value().equals(mymem, *oi, othermem))
                return false;
        }
    }
    return true;
}


_VarExtra* _VarMap::ensureExtra(TreeMem& mem)
{
    _checkmem(mem);
    _VarExtra* ex = _extra;
    if (!ex)
        _extra = ex = _NewExtra(mem, *this);
    return ex;
}

bool _VarMap::isExpired(u64 now) const
{
    return _extra && now < _extra->expiryTS;
}


// TODO: if we don't need a copying merge, make this a consuming merge that moves stuff
void _VarMap::merge(TreeMem& dstmem, const _VarMap& o, const TreeMem& srcmem, MergeFlags mergeflags)
{
    _checkmem(dstmem);
    for(Iterator it = o._storage.begin(); it != o._storage.end(); ++it)
    {
        PoolStr ps = srcmem.getSL(it.key());
        Var& dst = putKey(dstmem, ps.s, ps.len);

        const Var::Type othertype = it.value().type();

        if((mergeflags & MERGE_RECURSIVE) && othertype == Var::TYPE_MAP)
            dst.makeMap(dstmem)->merge(dstmem, *it.value().u.m, srcmem, mergeflags);
        else if((mergeflags & MERGE_APPEND_ARRAYS) && othertype == Var::TYPE_ARRAY && dst.type() == Var::TYPE_ARRAY)
        {
            const size_t oldsize = dst._size();
            const size_t addsize = it.value()._size();
            const Var *oa = it.value().array_unsafe();
            // resize destination and skip forward
            Var *a = dst.makeArray(dstmem, oldsize + addsize) + oldsize;
            for(size_t i = 0; i < addsize; ++i)
                a[i] = std::move(oa[i].clone(dstmem, srcmem));
        }
        else // One entry replaces the other entirely
        {
            dst.clear(dstmem);
            dst = std::move(it.value().clone(dstmem, srcmem)); // TODO: optimize if srcmem==dstmem?
        }
    }
}

void _VarMap::clear(TreeMem& mem)
{
    _checkmem(mem);
    for (_Map::iterator it = _storage.begin(); it != _storage.end(); ++it)
    {
        mem.freeS(it.key());
        it.value().clear(mem);
    }
    _storage.dealloc(mem);
}

_VarMap* _VarMap::clone(TreeMem& dstmem, const TreeMem& srcmem) const
{
    _checkmem(srcmem);
    _VarMap *cp = _NewMap(dstmem, size());
    cp->_extra = _extra ? _extra->clone(dstmem, *cp) : NULL;
    for(Iterator it = _storage.begin(); it != _storage.end(); ++it)
    {
        PoolStr k = srcmem.getSL(it.key());
        cp->_storage.at(dstmem, dstmem.put(k.s, k.len)) = std::move(it.value().clone(dstmem, srcmem));
    }
    return cp;
}

_VarMap::_Map& _VarMap::_ensureData() const
{
    if(_extra)
    {
        _checkmem(_extra->mem);
        std::shared_lock lock(_extra->mutex);
        if(isExpired(getExpiryTime()))
        {
            std::unique_lock ulock(lock);
            _Map& m = const_cast<_Map&>(_storage);
            m.clear(_extra->mem);
        }
    }

    return const_cast<_Map&>(_storage);
}

Var* _VarMap::get(StrRef k)
{
    return k ? _storage.getp(k) : NULL;
}

const Var* _VarMap::get(StrRef k) const
{
    return k ? _storage.getp(k) : NULL;
}

Var& _VarMap::putKey(TreeMem& mem, const char* key, size_t len)
{
    StrRef k = mem.putNoRefcount(key, len);
    return getOrCreate(mem, k);
}

Var& _VarMap::getOrCreate(TreeMem& mem, StrRef key)
{
    return _InsertAndRefcount(mem, _storage, key);
}

Var& _VarMap::put(TreeMem& mem, StrRef k, Var&& x)
{
    _checkmem(mem);
    _Map::InsertResult ins = _storage.insert(mem, k, std::move(x));
    if(ins.newly_inserted)
        mem.increfS(k);
    return ins.ref;
}

void _VarMap::setExtra(Extra* extra)
{
    assert(!!_extra);
    _extra = extra;
}

VarRef& VarRef::makeMap()
{
    v->makeMap(*mem);
    return *this;
}

VarRef& VarRef::makeArray(size_t n)
{
    v->makeArray(*mem, n);
    return *this;
}

VarRef VarRef::operator[](const char* key)
{
    Var& sub = v->makeMap(*mem)->putKey(*mem, key, strlen(key));
    return VarRef(*mem, &sub);
}

bool VarRef::merge(const VarCRef& o, MergeFlags mergeflags)
{
    assert(v && o.v);

    if(isNull())
    {
        *v = std::move(o.v->clone(*mem, *o.mem));
        return true;
    }

    if(o.type() != Var::TYPE_MAP)
        return false;

    const Var::Map& om = *o.v->map_unsafe();
    v->makeMap(*mem, om.size())->merge(*mem, om, *o.mem, mergeflags);
    return true;

}

void VarRef::replace(const VarCRef& o)
{
    assert(v && o.v);
    v->clear(*mem);
    *v = std::move(o.v->clone(*mem, *o.mem));
}

VarRef VarRef::at(size_t idx) const
{
    return VarRef(*mem, v->at(idx));
}

VarRef VarRef::lookup(const char* key) const
{
    return VarRef(*mem, v->lookup(mem->lookup(key, strlen(key))));
}

VarCRef VarCRef::at(size_t idx) const
{
    return VarCRef(*mem, v->at(idx));
}

VarCRef VarCRef::lookup(const char* key) const
{
    return VarCRef(*mem, v->lookup(mem->lookup(key, strlen(key))));
}

Var::CompareResult VarCRef::compare(Var::CompareMode cmp, const VarCRef& o)
{
    return v->compare(cmp, *mem, *o.v, *o.mem);
}

_VarExtra* _VarExtra::clone(TreeMem& mem, _VarMap& m)
{
    _VarExtra *e = _NewExtra(mem, m);
    e->expiryTS = expiryTS;
    return e;
}

_VarExtra::_VarExtra(_VarMap& m, TreeMem& mem)
    : expiryTS(0), mymap(m), mem(mem)
{
}

_VarExtra::~_VarExtra()
{
}
