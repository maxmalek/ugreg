#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <utility>
#include <limits>
#include "variant.h"

Var::Var()
    : meta(TYPE_NULL)
{
    static_assert((size_t(1) << (SHIFT_TOPBIT - 1u)) - 1u == SIZE_MASK,
        "SIZE_MASK might be weird, check this");
}

Var::~Var()
{
    clear();
}

Var::Var(Var&& v) noexcept
    : meta(v.meta), u(v.u)
{
    v.meta = TYPE_NULL;
}

Var::Var(const Var& o)
    : Var()
{
    o.cloneInto(*this);
}

Var& Var::operator=(const Var& o)
{
    o.cloneInto(*this);
    return *this;
}

Var& Var::operator=(Var&& o) noexcept
{
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

Var::Var(const char* s)
    : Var()
{
    setStr(s);
}

Var::Var(const char* s, size_t len)
    : Var()
{
    setStr(s, len);
}

void Var::_settop(Topbits top, size_t size)
{
    assert(size <= SIZE_MASK);
    assert(top != BITS_OTHER);
    _transmute((size_t(top) << SHIFT_TOP2BITS) | size);
}

void Var::_transmute(size_t newmeta)
{
    switch(_topbits())
    {
        case BITS_STRING:
            free(u.s); // TODO: string pool?
            break;
        case BITS_ARRAY:
            delete [] u.a;
            break;
        case BITS_MAP:
            delete u.m;
            break;
        case BITS_OTHER: ; // nothing to do
    }

    meta = newmeta;
}

void Var::clear()
{
    _transmute(TYPE_NULL);
}

void Var::cloneInto(Var& v) const
{
    v.clear();
    v.meta = this->meta;
    switch(_topbits())
    {
    case BITS_STRING:
        v.u.s = _strdup(u.s);
        break;
    case BITS_ARRAY:
    {
        const size_t N = _size();
        Var * const a = new Var[N];
        for(size_t i = 0; i < N; ++i)
            u.a[i].cloneInto(a[i]);
        v.u.a = a;
        break;
    }
    case BITS_MAP:
        v.u.m = u.m->clone();
        break;
    case BITS_OTHER:
        v.u = this->u;
        break;
    }
}

Var * Var::makeArray(size_t n)
{
    if(_topbits() == BITS_ARRAY)
        return u.a;
    _settop(BITS_ARRAY, n);
    return (( u.a = new Var[n] ));
}

Var::Map *Var::makeMap()
{
    if (_topbits() == BITS_MAP)
        return u.m;
    _settop(BITS_MAP, 0);
    return (( u.m = new Map ));
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

const char* Var::asString() const
{
    return _topbits() == BITS_STRING ? u.s : NULL;
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

Var* Var::lookup(const char* key)
{
    return _topbits() == BITS_MAP ? u.m->get(key) : NULL;
}

const Var* Var::lookup(const char* key) const
{
    return _topbits() == BITS_MAP ? u.m->get(key) : NULL;
}

Var* Var::lookup(const char* kbegin, size_t klen)
{
    return  _topbits() == BITS_MAP ? u.m->get(kbegin, klen) : NULL;
}

const Var* Var::lookup(const char* kbegin, size_t klen) const
{
    return  _topbits() == BITS_MAP ? u.m->get(kbegin, klen) : NULL;
}

inline Var& Var::operator[](const char* key)
{
    return (*map_unsafe())[key];
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

bool Var::setBool(bool x)
{
    _transmute(TYPE_BOOL);
    return (( u.ui = x ));
}

s64 Var::setInt(s64 x)
{
    _transmute(TYPE_INT);
    return (( u.i = x ));
}

u64 Var::setUint(u64 x)
{
    _transmute(TYPE_UINT);
    return ((u.ui = x));
}

double Var::setFloat(double x)
{
    _transmute(TYPE_FLOAT);
    return (( u.f = x ));
}

const char* Var::setStr(const char* x)
{
    return setStr(x, strlen(x));
}

const char* Var::setStr(const char* x, size_t len)
{
    char *s = (char*)malloc(len + 1);
    if(s)
    {
        memcpy(s, x, len);
        s[len] = 0;
        _settop(BITS_STRING, len);
    }
    else
        meta = 0;
    return (( u.s = s ));
}


_VarMap::_VarMap()
    : expirytime(0)
{
}

_VarMap::~_VarMap()
{
}

// TODO: if we don't need a copying merge, make this a consuming merge that moves stuff
void _VarMap::merge(const _VarMap& o)
{
    for(_Map::const_iterator it = o._storage.begin(); it != o._storage.end(); ++it)
    {
        Var& dst = _storage[it->first]; // Create key/value if it's not there yet

        if(dst.type() == Var::TYPE_MAP && it->second.type() == Var::TYPE_MAP)
            // Both are maps, merge recursively
            dst.u.m->merge(*it->second.u.m);
        else // One entry replaces the other entirely
            dst = it->second;
    }
}

void _VarMap::clear()
{
    _storage.clear();
}

_VarMap* _VarMap::clone() const
{
    _VarMap *cp = new _VarMap;
    cp->_storage = _storage;
    return cp;
}

Var* _VarMap::get(const char* key)
{
    _Map::iterator it = _storage.find(key);
    return it != _storage.end() ? &it->second : NULL;
}

const Var* _VarMap::get(const char* key) const
{
    _Map::const_iterator it = _storage.find(key);
    return it != _storage.end() ? &it->second : NULL;
}

const Var* _VarMap::get(const char* kbegin, size_t klen) const
{
    _Map::const_iterator it = _storage.find(std::string(kbegin, klen));
    return it != _storage.end() ? &it->second : NULL;
}

Var* _VarMap::get(const char* kbegin, size_t klen)
{
    _Map::iterator it = _storage.find(std::string(kbegin, klen));
    return it != _storage.end() ? &it->second : NULL;
}

inline Var& _VarMap::operator[](const char* key)
{
    return _storage[key];
}

void _VarMap::emplace(const char* kbegin, size_t klen, Var&& x)
{
    _storage.insert(std::make_pair(std::string(kbegin, klen), std::move(x)));
}

// TODO: make it so that Var doesn't operate on the global heap
// but instead uses an allocator inside its container (eg. DataTree)
// (that needs to be passed in where appropriate to avoid storing an extra pointer per Var)
// -> remove new, strdup, malloc, etc
