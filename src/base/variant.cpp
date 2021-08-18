#include <stdlib.h>
#include <assert.h>
#include <utility>
#include <limits>
#include <string.h>

#include "variant.h"
#include "treemem.h"
#include "mem.h"

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

static VarExpiry *_NewExpiry(TreeMem& mem)
{
    void *p = (VarExpiry*)mem.Alloc(sizeof(VarExpiry));
    VarExpiry *ex = _X_PLACEMENT_NEW(p) VarExpiry;
    return ex;
}

static void _DeleteExpiry(TreeMem& mem, VarExpiry *ex)
{
    ex->~VarExpiry();
    mem.Free(ex, sizeof(*ex));
}


Var::Var()
    : meta(TYPE_NULL)
{
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
}

Var& Var::operator=(Var&& o) noexcept
{
    assert(meta == TYPE_NULL); // Forgot to clear() first?
    meta = o.meta;
    u = o.u;
    o.meta = TYPE_NULL;
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
            break; // nothing to do
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
    dst.meta = this->meta;
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
        dst.u = this->u;
        break;
    }
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

const s64 *Var::asInt() const
{
    switch (meta)
    {
    case TYPE_UINT:
        if (u.ui > std::numeric_limits<s64>::max())
            break;
        // fall through
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
        // fall through
        case TYPE_UINT:
            return &u.ui;
    }
    return NULL;
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

inline Var& Var::operator[](size_t idx)
{
    assert(_topbits() == BITS_ARRAY);
    assert(idx < _size());
    return u.a[idx];
}

inline const Var& Var::operator[](size_t idx) const
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

size_t Var::size() const
{
    switch(_topbits())
    {
        case 0: // Not a container
            return 0;
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
    if(s)
        _settop(mem, BITS_STRING, len);
    else
        meta = 0;
    return (( u.s = s ));
}

void* Var::setPtr(TreeMem& mem, void* p)
{
    _transmute(mem, TYPE_PTR);
    return ((u.p = p));
}

void _VarMap::_checkmem(const TreeMem& m) const
{
#ifdef _DEBUG
    assert(_mymem == &m && "Allocator mismatch!");
#endif
}

_VarMap::_VarMap(TreeMem& mem)
    : _expiry(NULL)
#ifdef _DEBUG
    , _mymem(&mem)
#endif
{
}

_VarMap::_VarMap(_VarMap&& o)
    : _storage(std::move(o._storage))
    , _expiry(o._expiry)
{
    o._expiry = NULL;
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
    _Map::iterator f = storage.find(k);
    if (f == storage.end())
    {
        dstmem.increfS(k); // ... and refcount it if newly inserted
        f = storage.emplace_hint(f, k, Var());
        //return storage[k]; // this is fine too but the above should be faster
    }
    return f->second;
}

bool _VarMap::isExpired(u64 now) const
{
    return _expiry && now < _expiry->ts;
}


// TODO: if we don't need a copying merge, make this a consuming merge that moves stuff
void _VarMap::merge(TreeMem& dstmem, const _VarMap& o, const TreeMem& srcmem, MergeFlags mergeflags)
{
    _checkmem(dstmem);
    for(_Map::const_iterator it = o._storage.begin(); it != o._storage.end(); ++it)
    {
        PoolStr ps = srcmem.getSL(it->first);
        Var& dst = putKey(dstmem, ps.s, ps.len);

        const Var::Type othertype = it->second.type();

        if((mergeflags & MERGE_RECURSIVE) && othertype == Var::TYPE_MAP)
            dst.makeMap(dstmem)->merge(dstmem, *it->second.u.m, srcmem, mergeflags);
        else if((mergeflags & MERGE_APPEND_ARRAYS) && othertype == Var::TYPE_ARRAY && dst.type() == Var::TYPE_ARRAY)
        {
            const size_t oldsize = dst._size();
            const size_t addsize = it->second._size();
            const Var *oa = it->second.array_unsafe();
            // resize destination and skip forward
            Var *a = dst.makeArray(dstmem, oldsize + addsize) + oldsize;
            for(size_t i = 0; i < addsize; ++i)
                a[i] = std::move(oa[i].clone(dstmem, srcmem));
        }
        else // One entry replaces the other entirely
        {
            dst.clear(dstmem);
            dst = std::move(it->second.clone(dstmem, srcmem)); // TODO: optimize if srcmem==dstmem?
        }
    }
}

void _VarMap::clear(TreeMem& mem)
{
    _checkmem(mem);
    for (_Map::iterator it = _storage.begin(); it != _storage.end(); ++it)
    {
        mem.freeS(it->first);
        it->second.clear(mem);
    }
    _storage.clear();
}

_VarMap* _VarMap::clone(TreeMem& dstmem, const TreeMem& srcmem) const
{
    _checkmem(srcmem);
    _VarMap *cp = _NewMap(dstmem, size());
    cp->_expiry = _expiry ? _expiry->clone(dstmem) : NULL;
    for(Iterator it = _storage.begin(); it != _storage.end(); ++it)
    {
        PoolStr k = srcmem.getSL(it->first);
        cp->_storage[dstmem.put(k.s, k.len)] = it->second.clone(dstmem, srcmem);
    }
    return cp;
}

Var* _VarMap::get(StrRef k)
{
    _Map::iterator it = _storage.find(k);
    return it != _storage.end() ? &it->second : NULL;
}

const Var* _VarMap::get(StrRef k) const
{
    _Map::const_iterator it = _storage.find(k);
    return it != _storage.end() ? &it->second : NULL;
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

Var& _VarMap::emplace(TreeMem& mem, StrRef k, Var&& x)
{
    _checkmem(mem);
    auto it = _storage.insert(std::make_pair(k, std::move(x)));
    if(it.second)
        mem.increfS(k);
    return it.first->second;
}

VarRef& VarRef::makeMap()
{
    v->makeMap(mem);
    return *this;
}

VarRef& VarRef::makeArray(size_t n)
{
    v->makeArray(mem, n);
    return *this;
}

VarRef VarRef::operator[](const char* key)
{
    Var& sub = v->makeMap(mem)->putKey(mem, key, strlen(key));
    return VarRef(mem, &sub);
}

bool VarRef::merge(const VarCRef& o, MergeFlags mergeflags)
{
    assert(v && o.v);

    if(isNull())
    {
        *v = std::move(o.v->clone(mem, o.mem));
        return true;
    }

    if(o.type() != Var::TYPE_MAP)
        return false;

    const Var::Map& om = *o.v->map_unsafe();
    v->makeMap(mem, om.size())->merge(mem, om, o.mem, mergeflags);
    return true;

}

void VarRef::replace(const VarCRef& o)
{
    assert(v && o.v);
    v->clear(mem);
    *v = std::move(o.v->clone(mem, o.mem));
}

VarRef VarRef::at(size_t idx) const
{
    return VarRef(mem, v->at(idx));
}

VarRef VarRef::lookup(const char* key) const
{
    return VarRef(mem, v->lookup(mem.lookup(key, strlen(key))));
}

VarCRef VarCRef::at(size_t idx) const
{
    return VarCRef(mem, v->at(idx));
}

VarCRef VarCRef::lookup(const char* key) const
{
    return VarCRef(mem, v->lookup(mem.lookup(key, strlen(key))));
}

VarExpiry* VarExpiry::clone(TreeMem& mem)
{
    VarExpiry *cl = (VarExpiry*)mem.Alloc(sizeof(VarExpiry));
    cl->ts = ts;
    return cl;
}

VarExpiry::VarExpiry()
{
}

VarExpiry::~VarExpiry()
{
}
